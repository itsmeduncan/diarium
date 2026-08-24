#include "device/device_http.h"

#include <Arduino.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>

#include <utility>

#include "core/net/http_body.h"
#include "core/net/http_response.h"
#include "core/net/url.h"

namespace diarium {
namespace device {
namespace {

// A ByteSource over a live socket. Returns 0 when the peer closes, which the
// body framing reads as the end of the stream.
class SocketByteSource final : public ByteSource {
 public:
  SocketByteSource(WiFiClient* client, uint32_t timeout_ms)
      : client_(client), timeout_ms_(timeout_ms) {}

  size_t read(char* dst, size_t n) override {
    const uint32_t started = millis();
    while (client_->available() == 0) {
      if (!client_->connected()) return 0;
      if (millis() - started > timeout_ms_) {
        failed_ = true;
        return 0;
      }
      delay(5);
    }
    const int got = client_->read(reinterpret_cast<uint8_t*>(dst), n);
    if (got < 0) {
      failed_ = true;
      return 0;
    }
    return static_cast<size_t>(got);
  }

  bool failed() const override { return failed_; }

 private:
  WiFiClient* client_;
  uint32_t timeout_ms_;
  bool failed_ = false;
};

// Owns the socket for as long as the body is being read, so the caller can
// treat the response like any other ByteSource and let it fall out of scope.
class ResponseStream final : public ByteSource {
 public:
  ResponseStream(std::unique_ptr<WiFiClientSecure> tls, const HttpHead& head,
                 std::string prefetched, uint32_t timeout_ms, size_t max_bytes)
      : tls_(std::move(tls)),
        socket_(tls_.get(), timeout_ms),
        body_(socket_, head, std::move(prefetched)),
        limited_(body_, max_bytes) {}

  size_t read(char* dst, size_t n) override { return limited_.read(dst, n); }
  bool failed() const override { return limited_.failed(); }
  bool truncated() const { return limited_.truncated(); }

 private:
  std::unique_ptr<WiFiClientSecure> tls_;
  SocketByteSource socket_;
  HttpBodySource body_;
  LimitedByteSource limited_;
};

bool status_is_redirect(int status) {
  return status == 301 || status == 302 || status == 303 || status == 307 ||
         status == 308;
}

}  // namespace

std::unique_ptr<ByteSource> DeviceHttpClient::get(const HttpRequest& request,
                                                  HttpResponse* out) {
  HttpResponse discard;
  HttpResponse& res = out != nullptr ? *out : discard;
  res = HttpResponse();

  std::string url = request.url;

  for (int hop = 0; hop <= kMaxHops; ++hop) {
    Url parsed;
    if (!parse_url(url, &parsed)) {
      res.error = "not a URL this reader understands";
      return nullptr;
    }
    if (!parsed.secure) {
      // Plain HTTP would work, but silently downgrading a reader's traffic is
      // not something to do on their behalf.
      res.error = "http is not served; use https";
      return nullptr;
    }

    auto tls = std::unique_ptr<WiFiClientSecure>(new WiFiClientSecure());
    // Encrypted but unverified, deliberately. Validating a certificate
    // needs a CA bundle in flash and a correct clock, and this device learns
    // the date from the Date headers of the very servers it would be
    // validating — so at first boot the check would be circular. It fetches
    // public feeds and sends no credentials: `parse_url` discards any
    // userinfo, so nothing secret can ride along. This buys privacy from a
    // passive observer, not authenticity of the publisher.
    tls->setInsecure();
    tls->setTimeout(request.timeout_ms / 1000);

    if (!tls->connect(parsed.host.c_str(), parsed.port)) {
      res.error = "could not reach " + parsed.host;
      return nullptr;
    }

    std::string req = "GET " + parsed.path + " HTTP/1.1\r\n";
    req += "Host: " + parsed.host + "\r\n";
    req += "User-Agent: Diarium/1.0\r\n";
    req += "Accept: application/rss+xml, application/atom+xml, application/xml, text/xml\r\n";
    req += "Connection: close\r\n";
    if (!request.etag.empty()) req += "If-None-Match: " + request.etag + "\r\n";
    if (!request.last_modified.empty()) {
      req += "If-Modified-Since: " + request.last_modified + "\r\n";
    }
    req += "\r\n";
    tls->print(req.c_str());

    // Read the head, keeping whatever body arrived alongside it.
    HttpHeadParser parser;
    std::string buffered;
    char chunk[512];
    SocketByteSource socket(tls.get(), request.timeout_ms);
    while (!parser.done()) {
      const size_t n = socket.read(chunk, sizeof(chunk));
      if (n == 0) {
        res.error = parser.head().status == 0 ? "no response" : "head cut short";
        return nullptr;
      }
      buffered.append(chunk, n);
      if (!parser.feed(chunk, n)) {
        res.error = "response head too large";
        return nullptr;
      }
    }

    const HttpHead& head = parser.head();
    res.status = head.status;
    res.etag = head.etag;
    res.last_modified = head.last_modified;
    res.server_date = head.date;

    if (head.status == 304) {
      res.error.clear();  // not an error: nothing has changed
      return nullptr;
    }

    if (status_is_redirect(head.status)) {
      const std::string next = resolve_url(url, head.location);
      if (next.empty()) {
        res.error = "redirect went nowhere";
        return nullptr;
      }
      url = next;
      continue;  // the socket closes as `tls` falls out of scope
    }

    if (head.status < 200 || head.status > 299) {
      res.error = "server said " + std::to_string(head.status);
      return nullptr;
    }

    std::string prefetched = buffered.substr(parser.body_offset());
    return std::unique_ptr<ByteSource>(
        new ResponseStream(std::move(tls), head, std::move(prefetched),
                           request.timeout_ms, request.max_bytes));
  }

  res.error = "too many redirects";
  return nullptr;
}

}  // namespace device
}  // namespace diarium
