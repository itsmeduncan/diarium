// The device composition root: pick a lifecycle from the wake reason, build
// the HAL, run. No policy lives here — that is what src/core/ is for.
#include <Arduino.h>
#include <esp_sleep.h>

#include <memory>
#include <string>
#include <vector>

#include "Inkplate.h"
#include "core/edition/edition.h"
#include "core/edition/edition_store.h"
#include "core/config/feeds_config.h"
#include "core/text/font_pack.h"
#include "core/base/datetime.h"
#include "core/ui/contents.h"
#include "core/ui/notice.h"
#include "core/ui/gesture.h"
#include "core/ui/reader.h"
#include "core/ui/session.h"
#include "core/edition/seen_store.h"
#include "core/net/feed_cache.h"
#include "device/compose.h"
#include "device/device_hal.h"
#include "device/device_wifi.h"
#include "device/device_http.h"
#include "device/device_wifi.h"

using namespace rsspaper;

namespace {

Inkplate panel(INKPLATE_3BIT);

// Long-lived and large: these are what the firmware owns for a reading
// session, and they are deliberately not on the stack.
device::DeviceHal* hal_impl = nullptr;
FontPack fonts;
Edition edition;
Reader* reader = nullptr;
Session session{SessionThresholds{}};
// Kept from feeds.toml so the idle sleep can aim at the next edition.
std::string wake_at = "05:30";
GestureRecognizer gestures;

// Says what went wrong on the panel rather than only down the serial line,
// which nobody reading a newspaper is watching.
void show_notice(device::DeviceHal& d, const char* headline, const char* body) {
  render_notice(fonts, headline, body, &d.display.framebuffer());
  d.display.flush(RefreshMode::Full);
  Serial.printf("%s — %s\n", headline, body);
}

bool load_paper(device::DeviceHal& d) {
  std::string blob;
  if (!d.storage.read("/literata.rfp", &blob)) {
    show_notice(d, "No type", "The card has no literata.rfp on it.");
    return false;
  }
  std::vector<uint8_t> bytes(blob.begin(), blob.end());
  blob.clear();
  blob.shrink_to_fit();

  std::string error;
  if (!fonts.load(std::move(bytes), &error)) {
    show_notice(d, "No type", error.c_str());
    return false;
  }
  if (!d.storage.read("/edition.rspe", &blob)) {
    show_notice(d, "No paper yet",
                "The card has no edition on it. Compose one and copy it over.");
    return false;
  }
  if (!deserialize_edition(blob, &edition, &error)) {
    show_notice(d, "Paper unreadable", error.c_str());
    return false;
  }
  if (edition.pages.empty()) {
    show_notice(d, "No paper yet", "The edition on the card has no pages.");
    return false;
  }
  return true;
}

enum class Lifecycle { Compose, Read };

Lifecycle lifecycle_from_wake() {
  // An RTC alarm means it is time to build tomorrow's paper. Anything else —
  // the wake button, a reset, first power-on — means someone wants to read.
  // The scheduled wake is a timer; a touch is ext1 on TOUCHSCREEN_INT.
  return esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER
             ? Lifecycle::Compose
             : Lifecycle::Read;
}

// Accepts one file over the serial line in the moment after boot, so config
// can reach the card without the card leaving the device. Nothing here logs
// what it receives: this is how wifi credentials arrive.
//
//   host sends:  PUT <path> <bytes>\n  followed by exactly <bytes> bytes
//   device says: READY, then OK <bytes> or ERR <reason>
bool serial_receive(device::DeviceHal& d) {
  Serial.println("READY");  // the host waits for this before sending

  std::string header;
  const uint32_t deadline = millis() + 2000;
  while (millis() < deadline) {
    if (!Serial.available()) continue;
    const char c = static_cast<char>(Serial.read());
    if (c == '\n') break;
    if (c != '\r' && header.size() < 128) header.push_back(c);
  }
  if (header.compare(0, 4, "PUT ") != 0) return false;

  const size_t sp = header.find(' ', 4);
  if (sp == std::string::npos) {
    Serial.println("ERR malformed header");
    return false;
  }
  const std::string path = header.substr(4, sp - 4);
  const long want = atol(header.c_str() + sp + 1);
  if (want <= 0 || want > 256L * 1024) {
    Serial.println("ERR implausible length");
    return false;
  }

  std::string body;
  body.reserve(static_cast<size_t>(want));
  uint32_t last = millis();
  while (body.size() < static_cast<size_t>(want)) {
    if (Serial.available()) {
      body.push_back(static_cast<char>(Serial.read()));
      last = millis();
    } else if (millis() - last > 5000) {
      Serial.printf("ERR truncated at %u of %ld\n", (unsigned)body.size(), want);
      return false;
    }
  }

  if (!d.storage.write(path, body)) {
    Serial.println("ERR could not write to the card");
    return false;
  }
  Serial.printf("OK %u bytes to %s\n", (unsigned)body.size(), path.c_str());
  return true;
}

// Fetch, compose, persist, sleep. Never constructs a Reader, and the radio is
// off before the edition is laid out — the two must not be resident together
// (DECISIONS 27).
void compose_wake(device::DeviceHal& d) {
  Serial.println("compose wake");

  FeedList config;
  std::string toml;
  std::string config_error;
  if (!d.storage.read("/feeds.toml", &toml)) {
    Serial.println("no feeds.toml on the card — nothing to compose");
    d.power.deep_sleep_until(kNoDate);
  }
  if (!parse_feeds_toml(toml, &config, &config_error)) {
    Serial.printf("feeds.toml: %s\n", config_error.c_str());
    d.power.deep_sleep_until(kNoDate);
  }

  // The offset has to be in hand before anything is dated.
  d.clock.set_utc_offset(config.edition.utc_offset_minutes * 60);

  FeedCache cache;
  std::string blob;
  if (d.storage.read("/cache.dat", &blob)) cache.deserialize(blob);

  device::ComposeReport fetched;
  if (config.wifi.configured()) {
    device::DeviceWifi wifi;
    if (wifi.connect(config.wifi)) {
      device::fetch_all(config, &d.http, &d.storage, &cache, &d.clock, &fetched);
      wifi.disconnect();  // before anything heavy is built
      Serial.printf("fetched %u, unchanged %u, failed %u, %u bytes in %u ms\n",
                    (unsigned)fetched.feeds_fetched,
                    (unsigned)fetched.feeds_unchanged,
                    (unsigned)fetched.feeds_failed,
                    (unsigned)fetched.bytes_downloaded,
                    (unsigned)fetched.elapsed_ms);
      d.storage.write("/cache.dat", cache.serialize());
    } else {
      Serial.println("wifi: could not join — composing from what is cached");
    }
  } else {
    Serial.println("no [wifi] section — composing from what is cached");
  }

  // The font pack is needed to lay out, and only now that the radio is off.
  std::string fontblob;
  if (!d.storage.read("/literata.rfp", &fontblob)) {
    Serial.println("no font pack — cannot compose");
    d.power.deep_sleep_until(kNoDate);
  }
  std::vector<uint8_t> bytes(fontblob.begin(), fontblob.end());
  fontblob.clear();
  fontblob.shrink_to_fit();
  std::string error;
  if (!fonts.load(std::move(bytes), &error)) {
    Serial.printf("font pack: %s\n", error.c_str());
    d.power.deep_sleep_until(kNoDate);
  }

  // Dedup against what the reader has read, not against what the composer
  // printed. A story nobody got to is still news tomorrow; a story that was
  // read is finished with. max_age_days is what stops carry-over accumulating
  // past the point of being news at all.
  SeenStore read_state;
  if (d.storage.read("/read.dat", &blob)) {
    read_state.deserialize(blob, d.clock.now());
  }

  const uint32_t t0 = millis();
  // Local, not UTC: a paper is dated the day the reader is having, and at
  // nine in the evening those are not the same day.
  const Epoch local_now = d.clock.now() + d.clock.utc_offset_seconds();
  Edition ed = device::compose_from_card(config, fonts, &d.storage, &read_state,
                                         local_now, fetched);
  Serial.printf("composed %u pages, %u stories in %u ms\n",
                (unsigned)ed.pages.size(), (unsigned)ed.stories.size(),
                (unsigned)(millis() - t0));

  // A colophon and nothing else is not a paper. Composing twice in a morning
  // finds everything already seen, and replacing a good edition with an empty
  // one loses the reader their news for no gain.
  if (ed.stories.empty()) {
    Serial.println("nothing new — keeping the paper already on the card");
  } else if (d.storage.write("/edition.rspe", serialize_edition(ed))) {
    // The read state is the reader's to write, not the composer's: composing
    // must not mark anything read.
    Serial.println("saved");

    // Draw the new paper before sleeping. E-ink holds the last image, so
    // without this the panel keeps yesterday's cover and the reader picks up
    // a device that looks like nothing happened overnight.
    const std::vector<size_t> order = ed.reading_order();
    const std::vector<bool> unread(order.size(), true);
    render_contents(fonts, ed, order, unread, "composed on device",
                    &d.display.framebuffer());
    d.display.flush(RefreshMode::Full);
  } else {
    Serial.println("could not save the edition");
  }

  // The next edition is due at wake_at, local. Sleeping a flat day instead
  // means the paper arrives whenever you last put the device down, which is
  // how a morning paper ends up being yesterday's.
  const Epoch now_local = d.clock.now() + d.clock.utc_offset_seconds();
  const uint32_t until = seconds_until_local_time(config.edition.wake_at, now_local);
  Serial.printf("next edition in %u min (%s local)\n", (unsigned)(until / 60),
                config.edition.wake_at.c_str());
  d.clock.set_wake_alarm(d.clock.now() + static_cast<Epoch>(until));
  d.power.set_wake_in(until);
  d.power.deep_sleep_until(kNoDate);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(300);
  panel.begin();
  Serial.println("\nrsspaper");

  static device::DeviceHal d(&panel);  // claims the framebuffer first
  hal_impl = &d;

  if (!d.storage.mount()) {
    // No font pack is available either — the card is where it lives — so this
    // notice is the one that has to survive having no type at all.
    const bool unreadable = d.storage.state() == device::CardState::Unreadable;
    show_notice(d, unreadable ? "Card unreadable" : "No card",
                unreadable
                    ? "The card answers but its filesystem does not. Format it as exFAT."
                    : "Insert a card with feeds.toml and an edition on it.");
    return;
  }
  // Before anything else, in case the host has a file for us.
  serial_receive(d);

  if (lifecycle_from_wake() == Lifecycle::Compose) {
    compose_wake(d);  // does not return: it sleeps
  }

  const uint32_t t0 = millis();
  if (!load_paper(d)) return;
  Serial.printf("loaded in %u ms: %u pages, %u stories\n",
                (unsigned)(millis() - t0), (unsigned)edition.pages.size(),
                (unsigned)edition.stories.size());

  // What is actually on the card. A config that is not found should say so
  // rather than leaving the reader to wonder.
  {
    FsFile dir = panel.getSdFat().open("/", O_READ);
    FsFile entry;
    Serial.println("card contents:");
    char name[64];
    while (entry.openNext(&dir, O_READ)) {
      entry.getName(name, sizeof(name));
      Serial.printf("  %-28s %8u bytes%s\n", name, (unsigned)entry.fileSize(),
                    entry.isDir() ? "  <dir>" : "");
      entry.close();
    }
    dir.close();
  }

  // The offset travels on the card: no network to ask, no keyboard to be
  // asked. A missing or broken feeds.toml costs the local time, not the paper.
  FeedList config;
  std::string toml;
  std::string config_error;
  if (d.storage.read("/feeds.toml", &toml) &&
      parse_feeds_toml(toml, &config, &config_error)) {
    d.clock.set_utc_offset(config.edition.utc_offset_minutes * 60);
    wake_at = config.edition.wake_at;
    Serial.printf("feeds.toml: %u feeds, utc%+d min\n",
                  (unsigned)config.feeds.size(),
                  config.edition.utc_offset_minutes);
  } else if (toml.empty()) {
    Serial.println("feeds.toml: not on the card");
  } else {
    Serial.printf("feeds.toml: %s\n", config_error.c_str());
  }

  // Only a fallback now: a compose wake sets the clock from the servers it
  // talks to. Seeding from an edition's own date is how the device came to
  // believe it was permanently the day those fixtures were composed.
  d.clock.seed_if_unset(edition.date);
  d.input.begin();

  Hal hal = d.as_hal();
  static Reader r(edition, fonts, hal);
  reader = &r;
  reader->load_clippings("clippings.dat");
  reader->load_read_state("read.dat");
  reader->load_frontlight("light.dat");
  reader->render();
  session.touched(millis());
  Serial.printf("dated %s (rtc %ld)\n",
                format_masthead_date(edition.date).c_str(),
                (long)d.clock.now());
  Serial.printf("reading — %s\n", reader->position().c_str());
}

void loop() {
  if (reader == nullptr) { delay(1000); return; }

  TouchPoint p[2];
  const size_t n = hal_impl->input.poll(p, 2);
  const uint32_t now = millis();

  // Release coordinates are ignored by the recogniser, so a lifted finger is
  // reported as a release at the origin rather than a move to it.
  const GestureEvent e = n > 0 ? gestures.update(true, p[0].x, p[0].y, now)
                               : gestures.update(false, 0, 0, now);
  if (e.kind != Gesture::None) {
    session.touched(now);
    const bool changed = reader->handle(e);
    Serial.printf("gesture %d at %d,%d -> %s\n", (int)e.kind, e.x, e.y,
                  changed ? "changed" : "ignored");
    if (changed) {
      reader->tick();
      Serial.printf("  %s | blit %3u ms  refresh %4u ms\n",
                    reader->position().c_str(),
                    (unsigned)hal_impl->display.last_blit_ms(),
                    (unsigned)hal_impl->display.last_flush_ms());
    }
  }

  if (session.intent(now) == SessionIntent::Sleep) {
    // E-ink holds whatever was last drawn, so a sleeping paper sits on the
    // table showing this. Worth it being the nameplate rather than whichever
    // paragraph you happened to stop on.
    render_sleep_page(fonts, edition.title, format_masthead_date(edition.date),
                      &hal_impl->display.framebuffer());
    hal_impl->display.flush(RefreshMode::Full);

    // Always leave a scheduled wake armed, aimed at the next edition rather
    // than a flat day from now: a reader who puts the paper down at noon
    // should still get the morning one.
    const Epoch local = hal_impl->clock.now() + hal_impl->clock.utc_offset_seconds();
    hal_impl->power.set_wake_in(seconds_until_local_time(wake_at, local));
    Serial.println("sleeping — touch to wake");
    Serial.flush();
    hal_impl->power.deep_sleep_until(kNoDate);
  }
  delay(20);
}
