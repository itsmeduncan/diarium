// The device composition root: pick a lifecycle from the wake reason, build
// the HAL, run. No policy lives here — that is what src/core/ is for.
#include <Arduino.h>
#include <esp_sleep.h>

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "Inkplate.h"
#include "core/edition/edition.h"
#include "core/edition/edition_store.h"
#include "core/config/feeds_config.h"
#include "core/text/font_pack.h"
#include "core/base/datetime.h"
#include "core/ui/home.h"
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

using namespace diarium;

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

// A console on the serial line, open for a moment after boot.
//
// The device has no keyboard and its card does not come out, so this is how a
// file gets on, how the card is inspected, and how a compose is asked for
// without editing the firmware to force one. Nothing here logs what it
// receives: this is also how wifi credentials arrive.
//
//   PUT <path> <bytes>   then exactly that many bytes
//   LS                   what is on the card
//   RM <path>            remove one file
//   COMPOSE              take the compose path on this boot
//   GO                   stop listening and get on with it
//
// Replies are one line: OK ..., or ERR <reason>.
bool serial_console(device::DeviceHal& d) {
  // Only worth opening when a host might be there. Anything that talks to
  // this device resets it first, so a wake from deep sleep has nobody to wait
  // for — and 1.5 s is most of the time between a finger and a page.
  const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  if (cause == ESP_SLEEP_WAKEUP_EXT1 || cause == ESP_SLEEP_WAKEUP_TIMER) {
    return false;
  }

  Serial.println("READY");
  bool force_compose = false;

  uint32_t deadline = millis() + 1500;
  std::string line;

  while (millis() < deadline) {
    if (!Serial.available()) continue;
    const char c = static_cast<char>(Serial.read());
    if (c != '\n') {
      if (c != '\r' && line.size() < 160) line.push_back(c);
      continue;
    }

    // A command means someone is talking to us; keep listening for more.
    deadline = millis() + 4000;

    if (line == "GO") {
      line.clear();
      break;
    } else if (line == "COMPOSE") {
      force_compose = true;
      Serial.println("OK will compose");
    } else if (line == "LS") {
      FsFile dir = panel.getSdFat().open("/", O_READ);
      FsFile entry;
      char name[64];
      while (entry.openNext(&dir, O_READ)) {
        entry.getName(name, sizeof(name));
        Serial.printf("  %-28s %8u%s\n", name, (unsigned)entry.fileSize(),
                      entry.isDir() ? "  <dir>" : "");
        entry.close();
      }
      dir.close();
      Serial.println("OK");
    } else if (line.compare(0, 3, "RM ") == 0) {
      const std::string path = line.substr(3);
      Serial.println(d.storage.remove(path) ? "OK removed" : "ERR not removed");
    } else if (line.compare(0, 4, "PUT ") == 0) {
      const size_t sp = line.find(' ', 4);
      if (sp == std::string::npos) {
        Serial.println("ERR malformed header");
      } else {
        const std::string path = line.substr(4, sp - 4);
        const long want = atol(line.c_str() + sp + 1);
        if (want <= 0 || want > 256L * 1024) {
          Serial.println("ERR implausible length");
        } else {
          std::string body;
          body.reserve(static_cast<size_t>(want));
          uint32_t last = millis();
          bool ok = true;
          while (body.size() < static_cast<size_t>(want)) {
            if (Serial.available()) {
              body.push_back(static_cast<char>(Serial.read()));
              last = millis();
            } else if (millis() - last > 5000) {
              Serial.printf("ERR truncated at %u of %ld\n",
                            (unsigned)body.size(), want);
              ok = false;
              break;
            }
          }
          if (ok) {
            Serial.println(d.storage.write(path, body)
                               ? ("OK " + std::to_string(body.size()) +
                                  " bytes to " + path).c_str()
                               : "ERR could not write to the card");
          }
        }
      }
      deadline = millis() + 4000;
    } else if (!line.empty()) {
      Serial.println("ERR unknown command");
    }
    line.clear();
  }

  return force_compose;
}

// Fetch, compose, persist, sleep. Never constructs a Reader, and the radio is
// off before the edition is laid out — the two must not be resident together.
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

  // Before the edition is laid out: portrait or landscape is baked into the
  // pages, and the blits below rotate to match.
  set_orientation(config.edition.orientation);

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
    render_home(fonts, ed, order, unread, "composed on device",
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

// The battery reading has to reach the card, not only the serial line. The
// wakes worth measuring are the unattended ones, and a host that plugs in
// tomorrow to read a voltage has also put the cell back on charge, so it gets
// the charger's 4.2 V whatever the night actually cost. A day's draw is the
// difference between two of these.
void log_battery(device::DeviceHal& d, int mv) {
  std::string log;
  d.storage.read("/battery.log", &log);  // absent is fine: it starts empty

  // Bounded, like every other buffer here. A line is ~30 bytes and this is
  // appended a few times a day, but "a few" is an assumption, and an unbounded
  // file sharing a card with the edition is not acceptable.
  constexpr size_t kMaxBytes = 8192;
  if (log.size() > kMaxBytes) {
    const size_t cut = log.find('\n', log.size() - kMaxBytes);
    log = cut == std::string::npos ? std::string() : log.substr(cut + 1);
  }

  char line[64];
  snprintf(line, sizeof(line), "%lld %d\n",
           static_cast<long long>(d.clock.now()), mv);
  log += line;
  d.storage.write("/battery.log", log);
}

}  // namespace

void setup() {
  const uint32_t t_boot = millis();
  Serial.begin(115200);
  // Only a host that just reset us is listening; a woken reader is not.
  if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_UNDEFINED) delay(300);
  panel.begin();
  Serial.printf("[t] panel.begin %u\n", (unsigned)(millis() - t_boot));
  Serial.println("\ndiarium");

  static device::DeviceHal d(&panel);  // claims the framebuffer first
  hal_impl = &d;

  // On every boot, not only the ones with a host attached: the wakes between
  // two mornings are exactly the ones nobody is watching. Read before the card
  // is mounted, because a flat battery is a plausible reason for the card to
  // fail and this should be on the serial line either way.
  const int battery_mv = d.power.battery_millivolts();
  Serial.printf("battery %d mV\n", battery_mv);

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

  // After the mount, before anything that can sleep: compose_wake() does not
  // return, so a wake logged any later would not be logged at all.
  log_battery(d, battery_mv);

  // Before anything else, in case the host has something to say.
  uint32_t t_mark = millis();
  const bool asked_to_compose = serial_console(d);
  Serial.printf("[t] console %u\n", (unsigned)(millis() - t_mark));

  if (asked_to_compose || lifecycle_from_wake() == Lifecycle::Compose) {
    compose_wake(d);  // does not return: it sleeps
  }

  t_mark = millis();
  const uint32_t t0 = millis();
  if (!load_paper(d)) return;
  Serial.printf("[t] load_paper %u\n", (unsigned)(millis() - t_mark));
  Serial.printf("loaded in %u ms: %u pages, %u stories\n",
                (unsigned)(millis() - t0), (unsigned)edition.pages.size(),
                (unsigned)edition.stories.size());

  // What is actually on the card. A config that is not found should say so
  // rather than leaving the reader to wonder. Diagnostics for a host, so it
  // is skipped on the wakes where nobody is reading the serial line.
  if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_UNDEFINED) {
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
    // The reader's furniture must land at the same geometry the edition was
    // composed under. A broken config falls back to landscape, below.
    set_orientation(config.edition.orientation);
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

  // The framebuffer was built at the default geometry before the card told us
  // the orientation; make it match now. Without this the reader draws a
  // 758-wide portrait page into the 1024-wide buffer claimed at boot, and the
  // blit rotates the mismatch into garbage — a landscape page smeared into
  // portrait. Cheap: portrait and landscape are the same pixel count.
  d.display.framebuffer().resize(page_width(), page_height());

  Hal hal = d.as_hal();
  static Reader r(edition, fonts, hal);
  reader = &r;
  reader->load_read_state("read.dat");
  reader->load_frontlight("light.dat");
  t_mark = millis();
  reader->render();
  Serial.printf("[t] first render %u\n", (unsigned)(millis() - t_mark));
  Serial.printf("[t] TOTAL to a visible page %u\n", (unsigned)(millis() - t_boot));
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
