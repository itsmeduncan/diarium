// The hardware seam. Six interfaces, and nothing else in the project knows a
// device exists.
//
// This header is itself portable — it includes no platform headers and never
// will. `src/sim/` implements it with PNG output and a keyboard; `src/device/`
// implements it against the Inkplate library. Porting to another ESP32-class
// e-ink board means writing one more implementation and changing nothing above
// this line.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "core/base/datetime.h"
#include "core/io/byte_sink.h"
#include "core/io/byte_source.h"
#include "core/render/framebuffer.h"

namespace diarium {

// How to push the framebuffer to the panel.
//
// Measured on a 6FLICK: a 1-bit partial refresh takes ~0.46 s and a 3-bit
// greyscale full refresh ~1.74 s. Partial is what makes page turns feel like
// paper; full is what clears the ghosting partial leaves behind. Choosing
// between them is a policy decision, made above the HAL.
//
// The mode is part of the contract, not an implementation detail: the panel
// driver ignores a partial update unless it is in 1-bit mode.
enum class RefreshMode : uint8_t {
  Partial,   // fast, 1-bit, accumulates ghosting
  Full,      // slow, 3-bit greyscale, clears the panel
  DeepClean, // full refresh with extra flashing; for when ghosting has built up
};

class IDisplay {
 public:
  virtual ~IDisplay() = default;

  virtual int width() const = 0;
  virtual int height() const = 0;

  // The buffer the renderer draws into. Owned by the display so an
  // implementation can put it wherever it needs to — PSRAM, on device.
  virtual Framebuffer& framebuffer() = 0;

  virtual void flush(RefreshMode mode) = 0;

  // 0 turns the frontlight off; the 6FLICK has 64 steps above that.
  virtual void set_frontlight(int level) = 0;
  virtual int frontlight() const = 0;
  virtual int max_frontlight() const { return 63; }
};

struct TouchPoint {
  int x = 0;
  int y = 0;
};

class IInput {
 public:
  virtual ~IInput() = default;

  // Fills `out` with the currently-touched points and returns how many there
  // were. Zero means nothing is touching the panel. Gesture recognition is
  // portable and sits above this, so a HAL never has to agree about what a
  // swipe is.
  virtual size_t poll(TouchPoint* out, size_t max) = 0;

  // Milliseconds since boot. Gestures need a clock finer than wall time.
  virtual uint32_t millis() const = 0;
};

class IClock {
 public:
  virtual ~IClock() = default;
  virtual Epoch now() const = 0;
  // Local offset from UTC in seconds, for `wake_at` and the masthead date.
  virtual int utc_offset_seconds() const { return 0; }
  virtual void set_wake_alarm(Epoch when) = 0;
};

class IPower {
 public:
  virtual ~IPower() = default;
  // Does not return on real hardware: the device wakes from reset.
  virtual void deep_sleep_until(Epoch when) = 0;
  virtual int battery_millivolts() const = 0;
  virtual bool on_external_power() const { return false; }
};

class IStorage {
 public:
  virtual ~IStorage() = default;
  virtual bool read(const std::string& path, std::string* out) = 0;
  virtual bool write(const std::string& path, const std::string& data) = 0;
  virtual bool exists(const std::string& path) = 0;
  virtual bool remove(const std::string& path) = 0;

  // Open a path for streaming writes. Returns null on failure. Writing goes
  // through the returned ByteSink; destroying it closes the file. Lets a
  // caller append a file — an edition, a fetched feed — bigger than it wants
  // to hold resident, one chunk at a time.
  virtual std::unique_ptr<ByteSink> open_write(const std::string& path) = 0;
  // Total size of a file, or 0 if absent.
  virtual size_t size(const std::string& path) = 0;
  // Read [offset, offset+length) into out. False if the range runs past the
  // file, or the file is absent. Never returns more than the file holds.
  virtual bool read_range(const std::string& path, size_t offset,
                          size_t length, std::string* out) = 0;
};

struct HttpRequest {
  std::string url;
  // Conditional GET. Empty means unconditional.
  std::string etag;
  std::string last_modified;
  size_t max_bytes = 4u * 1024 * 1024;
  int timeout_ms = 15000;
};

struct HttpResponse {
  int status = 0;  // 0 means the request never completed
  std::string etag;
  std::string last_modified;
  // The server's Date header. A device with no NTP and an RTC that starts
  // unset has to learn the day from somewhere, and every response carries it.
  std::string server_date;
  std::string error;  // human-readable, for the colophon
};

class IHttpClient {
 public:
  virtual ~IHttpClient() = default;

  // Returns a stream over the response body, or null on failure or 304.
  // `out` is filled either way, so a caller can tell "not modified" from
  // "the wifi is down" — they mean very different things to a reader.
  virtual std::unique_ptr<ByteSource> get(const HttpRequest& request,
                                          HttpResponse* out) = 0;
};

// Everything the firmware needs from the world, in one place.
struct Hal {
  IDisplay* display = nullptr;
  IInput* input = nullptr;
  IClock* clock = nullptr;
  IPower* power = nullptr;
  IStorage* storage = nullptr;
  IHttpClient* http = nullptr;

  bool complete() const {
    return display != nullptr && input != nullptr && clock != nullptr &&
           power != nullptr && storage != nullptr && http != nullptr;
  }
};

}  // namespace diarium
