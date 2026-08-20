// The device composition root. Nothing above src/hal/ knows this file exists.
#include <Arduino.h>

#include <string>

#include "Inkplate.h"
#include "device/device_display.h"
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

  // Constructed first: the 776 KB framebuffer needs a contiguous PSRAM block,
  // and anything loaded before it fragments PSRAM enough to fail the claim.
  static device::DeviceDisplay display(&panel);
  Serial.printf("framebuffer: %dx%d, psram free %u\n", display.width(),
                display.height(), (unsigned)ESP.getFreePsram());

  static device::DeviceStorage storage(&panel);
  Serial.printf("card: %s\n", storage.mount() ? "mounted" : "unavailable");

  Framebuffer& fb = display.framebuffer();
  fb.fill(kPaper);
  fb.frame_rect(40, 40, fb.width() - 80, fb.height() - 80, kInk);
  fb.fill_rect(100, 100, 200, 100, 96);
  display.flush(RefreshMode::Full);
  Serial.printf("full    blit %3u ms  refresh %4u ms\n",
                (unsigned)display.last_blit_ms(), (unsigned)display.last_flush_ms());

  fb.fill_rect(100, 300, 200, 100, 32);
  display.flush(RefreshMode::Partial);
  Serial.printf("partial blit %3u ms  refresh %4u ms\n",
                (unsigned)display.last_blit_ms(), (unsigned)display.last_flush_ms());

  fb.fill_rect(400, 300, 200, 100, kInk);
  display.flush(RefreshMode::Partial);
  Serial.printf("partial blit %3u ms  refresh %4u ms\n",
                (unsigned)display.last_blit_ms(), (unsigned)display.last_flush_ms());
}

void loop() {}
