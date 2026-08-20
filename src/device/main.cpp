// The device composition root. Nothing above src/hal/ knows this file exists.
#include <Arduino.h>

#include "Inkplate.h"

namespace {
Inkplate panel(INKPLATE_3BIT);
}

void setup() {
  Serial.begin(115200);
  delay(300);
  panel.begin();
  Serial.printf("rsspaper: panel %dx%d\n", panel.width(), panel.height());
}

void loop() {}
