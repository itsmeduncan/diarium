// The device composition root. Nothing above src/hal/ knows this file exists.
#include <Arduino.h>

#include <string>

#include "Inkplate.h"
#include "device/device_storage.h"

using namespace rsspaper;

namespace {
Inkplate panel(INKPLATE_3BIT);
}

void setup() {
  Serial.begin(115200);
  delay(300);
  panel.begin();
  Serial.printf("\nrsspaper: panel %dx%d\n", panel.width(), panel.height());

  static device::DeviceStorage storage(&panel);
  if (!storage.mount()) {
    Serial.printf("card: %s\n", storage.state() == device::CardState::NoCard
                                    ? "absent" : "unreadable");
    return;
  }
  Serial.println("card: mounted");
  std::string blob;
  Serial.printf("literata.rfp present: %d\n", (int)storage.exists("/literata.rfp"));
  Serial.printf("edition.rspe present: %d\n", (int)storage.exists("/edition.rspe"));
  storage.write("/smoke.txt", "hello");
  storage.read("/smoke.txt", &blob);
  Serial.printf("round-trip: %s\n", blob == "hello" ? "OK" : "FAILED");
  storage.remove("/smoke.txt");
  Serial.printf("smoke.txt removed: %d\n", (int)!storage.exists("/smoke.txt"));
}

void loop() {}
