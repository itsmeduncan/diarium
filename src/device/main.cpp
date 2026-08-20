// The device composition root. Nothing above src/hal/ knows this file exists.
#include <Arduino.h>

#include <string>

#include "Inkplate.h"
#include "device/device_clock.h"
#include "device/device_display.h"
#include "device/device_http.h"
#include "device/device_input.h"
#include "device/device_power.h"
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

  // Verify the packing round-trips before trusting anything drawn with it:
  // a wrong nibble order would be exactly as fast and completely wrong.
  {
    Framebuffer& v = display.framebuffer();
    v.fill(kPaper);
    v.set(0, 0, 0);     // 0>>5 = 0, even x -> high nibble
    v.set(1, 0, 255);   // 255>>5 = 7, odd x -> low nibble
    v.set(2, 0, 96);    // 96>>5 = 3, even x -> high nibble
    display.flush(RefreshMode::Full);
    const uint8_t b0 = panel.DMemory4Bit[0];
    const uint8_t b1 = panel.DMemory4Bit[1];
    Serial.printf("3bit pack: byte0=0x%02X (want 0x07) byte1=0x%02X (want 0x37) %s\n",
                  b0, b1, (b0 == 0x07 && b1 == 0x37) ? "OK" : "WRONG");

    v.fill(kPaper);
    v.set(0, 0, 0);     // ink -> bit 0
    v.set(3, 0, 0);     // ink -> bit 3
    display.flush(RefreshMode::Partial);
    const uint8_t p0 = panel._partial[0];
    Serial.printf("1bit pack: byte0=0x%02X (want 0x09) %s\n", p0,
                  p0 == 0x09 ? "OK" : "WRONG");
  }

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

  static device::DeviceInput input(&panel);
  static device::DeviceClock clock_(&panel);
  static device::DevicePower power(&panel);
  static device::DeviceHttpClient http;
  power.set_storage(&storage);

  Serial.printf("touch init: %d\n", (int)input.begin());
  Serial.printf("rtc epoch: %ld\n", (long)clock_.now());
  Serial.printf("battery: %d mV\n", power.battery_millivolts());

  HttpResponse r;
  const bool stubbed = http.get(HttpRequest{}, &r) == nullptr;
  Serial.printf("http stub: null=%d status=%d error=%s\n", (int)stubbed,
                r.status, r.error.c_str());

  Hal hal;
  hal.display = &display;
  hal.input = &input;
  hal.clock = &clock_;
  hal.power = &power;
  hal.storage = &storage;
  hal.http = &http;
  Serial.printf("hal complete: %d\n", (int)hal.complete());

  Serial.println("touch the panel:");
  for (int i = 0; i < 60; ++i) {
    TouchPoint p[2];
    const size_t n = input.poll(p, 2);
    if (n > 0) Serial.printf("  touch %u at %d,%d\n", (unsigned)n, p[0].x, p[0].y);
    delay(100);
  }
  Serial.println("done.");
}

void loop() {}
