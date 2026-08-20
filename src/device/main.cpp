// The device composition root: pick a lifecycle from the wake reason, build
// the HAL, run. No policy lives here — that is what src/core/ is for.
#include <Arduino.h>
#include <esp_sleep.h>

#include <string>
#include <vector>

#include "Inkplate.h"
#include "core/edition/edition.h"
#include "core/edition/edition_store.h"
#include "core/config/feeds_config.h"
#include "core/text/font_pack.h"
#include "core/ui/notice.h"
#include "core/ui/gesture.h"
#include "core/ui/reader.h"
#include "core/ui/session.h"
#include "device/device_hal.h"

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
  // Both wake sources are ext1, so ask which pin actually fired.
  if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_EXT1) {
    return Lifecycle::Read;
  }
  const uint64_t fired = esp_sleep_get_ext1_wakeup_status();
  return (fired & (1ULL << GPIO_NUM_39)) ? Lifecycle::Compose : Lifecycle::Read;
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
  if (lifecycle_from_wake() == Lifecycle::Compose) {
    // Never constructs a Reader and never holds an Edition while the radio is
    // up. That separation is what keeps issue #3 inside the internal-RAM
    // budget; until #3 lands there is nothing to fetch, so re-arm and sleep.
    Serial.println("compose wake: no network yet");
    d.clock.set_wake_alarm(d.clock.now() + 24 * 60 * 60);
    d.power.deep_sleep_until(kNoDate);
  }

  const uint32_t t0 = millis();
  if (!load_paper(d)) return;
  Serial.printf("loaded in %u ms: %u pages, %u stories\n",
                (unsigned)(millis() - t0), (unsigned)edition.pages.size(),
                (unsigned)edition.stories.size());

  // The offset travels on the card: no network to ask, no keyboard to be
  // asked. A missing or broken feeds.toml costs the local time, not the paper.
  FeedList config;
  std::string toml;
  std::string config_error;
  if (d.storage.read("/feeds.toml", &toml) &&
      parse_feeds_toml(toml, &config, &config_error)) {
    d.clock.set_utc_offset(config.edition.utc_offset_minutes * 60);
    Serial.printf("feeds.toml: %u feeds, utc%+d min\n",
                  (unsigned)config.feeds.size(),
                  config.edition.utc_offset_minutes);
  } else if (!toml.empty()) {
    Serial.printf("feeds.toml: %s\n", config_error.c_str());
  }

  d.clock.seed_if_unset(edition.date);
  d.input.begin();

  Hal hal = d.as_hal();
  static Reader r(edition, fonts, hal);
  reader = &r;
  reader->load_clippings("clippings.dat");
  reader->render();
  session.touched(millis());
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
      Serial.printf("  %s\n", reader->position().c_str());
    }
  }

  if (session.intent(now) == SessionIntent::Sleep) {
    Serial.println("sleeping");
    Serial.flush();
    hal_impl->power.deep_sleep_until(kNoDate);
  }
  delay(20);
}
