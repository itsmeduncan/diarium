// The device composition root: pick a lifecycle from the wake reason, build
// the HAL, run. No policy lives here — that is what src/core/ is for.
#include <Arduino.h>

#include <string>
#include <vector>

#include "Inkplate.h"
#include "core/edition/edition.h"
#include "core/edition/edition_store.h"
#include "core/text/font_pack.h"
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

bool load_paper(device::DeviceHal& d) {
  std::string blob;
  if (!d.storage.read("/literata.rfp", &blob)) {
    Serial.println("no font pack on the card");
    return false;
  }
  std::vector<uint8_t> bytes(blob.begin(), blob.end());
  blob.clear();
  blob.shrink_to_fit();

  std::string error;
  if (!fonts.load(std::move(bytes), &error)) {
    Serial.printf("font pack: %s\n", error.c_str());
    return false;
  }
  if (!d.storage.read("/edition.rspe", &blob)) {
    Serial.println("no edition on the card");
    return false;
  }
  if (!deserialize_edition(blob, &edition, &error)) {
    Serial.printf("edition: %s\n", error.c_str());
    return false;
  }
  return !edition.pages.empty();
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
    Serial.printf("card %s\n", d.storage.state() == device::CardState::NoCard
                                   ? "absent" : "unreadable");
    return;
  }
  const uint32_t t0 = millis();
  if (!load_paper(d)) return;
  Serial.printf("loaded in %u ms: %u pages, %u stories\n",
                (unsigned)(millis() - t0), (unsigned)edition.pages.size(),
                (unsigned)edition.stories.size());

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
    if (reader->handle(e)) {
      reader->tick();
      Serial.printf("%s\n", reader->position().c_str());
    }
  }

  if (session.intent(now) == SessionIntent::Sleep) {
    Serial.println("sleeping");
    Serial.flush();
    hal_impl->power.deep_sleep_until(kNoDate);
  }
  delay(20);
}
