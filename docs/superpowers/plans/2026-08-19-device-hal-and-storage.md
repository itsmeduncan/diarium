# Device HAL and Storage Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A 6FLICK that boots, reads a composed edition off the microSD card, and lets you read it by touch — with all six HAL interfaces implemented and both lifecycles wired.

**Architecture:** `src/device/` holds one file per HAL interface plus a composition root that branches on wake reason. Power tiering lives in portable `src/core/ui/session.h` so it is testable in the simulator. Storage is the microSD card; flash holds only firmware.

**Tech Stack:** PlatformIO 6.1.19, `board = esp32dev` with `-DARDUINO_INKPLATE6FLICK`, Arduino ESP32 core 2.0.17, `e-radionicacom/InkplateLibrary@^11.1.3` (LGPL-3.0), SdFat over exFAT.

**Spec:** `docs/superpowers/specs/2026-08-19-inkplate-firmware-design.md`

## Global Constraints

- **`src/core/` and `src/hal/` must never include a platform header.** `tools/check-portability.sh` enforces it and gates CI. `src/device/` is exempt and is not scanned.
- **Neither build system globs `src/device/`.** The Makefile globs `src/core`, `src/sim src/hal`, `test`, `tools/fontgen`, `tools/hyphgen`; CMake globs the same. Adding `src/device/` cannot break the desktop build. Do not add it to either.
- **Allocation order is load-bearing.** Claim the 776 KB framebuffer before the font pack and edition, or `operator new` throws with 1.6 MB of PSRAM still free.
- **Flash budget is 4 MB.** App partition 0x220000 (2.125 MB), data unused (storage is the card).
- **Internal RAM is the scarce resource.** A resident `Edition` costs ~221 KB, leaving ~69 KB. Never bring up WiFi on the read path.
- **Sizes are physical.** The panel is ~212 PPI. Do not pick pixel sizes by how they look on a monitor.
- **Panel geometry is 1024x758**, matching `kPageWidth`/`kPageHeight`.
- **Serial is 115200.**

---

### Task 1: Device build target

**Files:**
- Create: `src/device/platformio.ini`
- Create: `src/device/partitions_rsspaper.csv`
- Create: `src/device/main.cpp`
- Modify: `Makefile` (add a `device` target; do not touch the glob variables)
- Modify: `.gitignore` (add `src/device/.pio/`)

**Interfaces:**
- Consumes: nothing.
- Produces: a `make device` target that builds firmware linking `src/core/`. Later tasks add files to `src/device/` and are picked up automatically by PlatformIO's `src_dir` glob.

- [ ] **Step 1: Create the partition table**

`src/device/partitions_rsspaper.csv` — 4 MB flash, no OTA, data lives on the card:

```
# Name,     Type, SubType,  Offset,   Size
nvs,        data, nvs,      0x9000,   0x5000
phy_init,   data, phy,      0xe000,   0x1000
factory,    app,  factory,  0x10000,  0x220000
spiffs,     data, spiffs,   0x230000, 0x1C0000
coredump,   data, coredump, 0x3F0000, 0x10000
```

- [ ] **Step 2: Create `src/device/platformio.ini`**

`src_dir = .` keeps device sources here; `-I../..` makes `#include "core/..."` resolve against the repo root.

```ini
[env:inkplate6flick]
platform = espressif32
board = esp32dev
framework = arduino
board_build.partitions = partitions_rsspaper.csv
src_dir = .
build_flags =
    -DARDUINO_INKPLATE6FLICK
    -DBOARD_HAS_PSRAM
    -mfix-esp32-psram-cache-issue
    -std=gnu++17
    -I../..
build_unflags = -std=gnu++11
build_src_filter = +<*.cpp> +<../core/**/*.cpp>
lib_deps = e-radionicacom/InkplateLibrary@^11.1.3
monitor_speed = 115200
```

- [ ] **Step 3: Write a minimal `src/device/main.cpp`**

```cpp
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
```

- [ ] **Step 4: Add the `device` target to the Makefile**

Append. It must not appear in `all` — a clone with only a compiler still has to build.

```make
.PHONY: device
device:
	cd src/device && pio run
```

- [ ] **Step 5: Build it**

Run: `make device`
Expected: `SUCCESS`, and the size line shows the factory partition is 2228224 bytes. Confirm `Compiling .pio/build/inkplate6flick/src/../core/...` lines appear — the core must be in this build.

- [ ] **Step 6: Verify the desktop build is untouched**

Run: `make clean && make && make check`
Expected: 155 tests pass, `portability: src/core and src/hal are clean`.

- [ ] **Step 7: Commit**

```bash
git add src/device Makefile .gitignore
git commit -m "Add the device build target"
```

---

### Task 2: DeviceStorage

**Files:**
- Create: `src/device/device_storage.h`, `src/device/device_storage.cpp`
- Modify: `src/device/main.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces: `class DeviceStorage final : public IStorage` with `explicit DeviceStorage(Inkplate* panel)`, plus `bool mount()` returning false when the card is absent or the filesystem is unreadable, `bool format()`, and `enum class CardState { Ok, NoCard, Unreadable }` via `CardState state() const`.

- [ ] **Step 1: Write the header**

```cpp
// IStorage over the microSD card. The card is the storage substrate: flash
// holds firmware only, so the scarce 4 MB stays free and OTA stays possible.
#pragma once

#include <string>

#include "Inkplate.h"
#include "hal/hal.h"

namespace rsspaper {
namespace device {

// A card that will not mount is a normal state, not an exception: the first
// card used on this project was HFS+ and unreadable while answering perfectly
// at block level.
enum class CardState { Ok, NoCard, Unreadable };

class DeviceStorage final : public IStorage {
 public:
  explicit DeviceStorage(Inkplate* panel) : panel_(panel) {}

  bool mount();
  bool format();
  CardState state() const { return state_; }

  bool read(const std::string& path, std::string* out) override;
  bool write(const std::string& path, const std::string& data) override;
  bool exists(const std::string& path) override;
  bool remove(const std::string& path) override;

 private:
  Inkplate* panel_;
  CardState state_ = CardState::NoCard;
};

}  // namespace device
}  // namespace rsspaper
```

- [ ] **Step 2: Write the implementation**

```cpp
#include "device/device_storage.h"

namespace rsspaper {
namespace device {

bool DeviceStorage::mount() {
  if (panel_->sdCardInit()) {
    state_ = CardState::Ok;
    return true;
  }
  SdFat& sd = panel_->getSdFat();
  const bool answers = sd.card() != nullptr && sd.card()->sectorCount() > 0;
  state_ = answers ? CardState::Unreadable : CardState::NoCard;
  return false;
}

bool DeviceStorage::format() {
  if (state_ != CardState::Unreadable) return false;
  if (!panel_->getSdFat().format(&Serial)) return false;
  return mount();
}

bool DeviceStorage::read(const std::string& path, std::string* out) {
  if (state_ != CardState::Ok) return false;
  FsFile f = panel_->getSdFat().open(path.c_str(), O_READ);
  if (!f) return false;
  const size_t n = f.size();
  out->resize(n);
  const size_t got = n == 0 ? 0 : f.read(&(*out)[0], n);
  f.close();
  return got == n;
}

bool DeviceStorage::write(const std::string& path, const std::string& data) {
  if (state_ != CardState::Ok) return false;
  FsFile f = panel_->getSdFat().open(path.c_str(), O_WRITE | O_CREAT | O_TRUNC);
  if (!f) return false;
  const size_t put = data.empty() ? 0 : f.write(data.data(), data.size());
  f.close();
  return put == data.size();
}

bool DeviceStorage::exists(const std::string& path) {
  return state_ == CardState::Ok && panel_->getSdFat().exists(path.c_str());
}

bool DeviceStorage::remove(const std::string& path) {
  return state_ == CardState::Ok && panel_->getSdFat().remove(path.c_str());
}

}  // namespace device
}  // namespace rsspaper
```

- [ ] **Step 3: Exercise it from `main.cpp`**

Replace the body of `setup()` after `panel.begin()`:

```cpp
  static device::DeviceStorage storage(&panel);
  if (!storage.mount()) {
    Serial.printf("card: %s\n", storage.state() == device::CardState::NoCard
                                    ? "absent" : "unreadable");
  } else {
    Serial.println("card: mounted");
    std::string blob;
    Serial.printf("literata.rfp present: %d\n", storage.exists("/literata.rfp"));
    storage.write("/smoke.txt", "hello");
    storage.read("/smoke.txt", &blob);
    Serial.printf("round-trip: %s\n", blob == "hello" ? "OK" : "FAILED");
    storage.remove("/smoke.txt");
  }
```

- [ ] **Step 4: Verify on device**

Run: `cd src/device && pio run -t upload && pio device monitor`
Expected:
```
card: mounted
literata.rfp present: 1
round-trip: OK
```

- [ ] **Step 5: Verify the unreadable path**

Physically remove the card, re-run. Expected: `card: absent`. Reinsert before continuing.

- [ ] **Step 6: Commit**

```bash
git add src/device/device_storage.h src/device/device_storage.cpp src/device/main.cpp
git commit -m "Implement IStorage over the microSD card"
```

---

### Task 3: DeviceDisplay

**Files:**
- Create: `src/device/device_display.h`, `src/device/device_display.cpp`
- Modify: `src/device/main.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces: `class DeviceDisplay final : public IDisplay` with `explicit DeviceDisplay(Inkplate* panel)` and `uint32_t last_flush_ms() const`. Its `Framebuffer&` must be constructed before any other large allocation.

- [ ] **Step 1: Write the header**

```cpp
// IDisplay against the 6FLICK panel. The framebuffer is 8-bit and lives in
// PSRAM; the panel shows 3 bits, so flush reduces on the way out.
#pragma once

#include <cstdint>

#include "Inkplate.h"
#include "core/render/framebuffer.h"
#include "hal/hal.h"

namespace rsspaper {
namespace device {

class DeviceDisplay final : public IDisplay {
 public:
  explicit DeviceDisplay(Inkplate* panel) : panel_(panel) {}

  int width() const override { return fb_.width(); }
  int height() const override { return fb_.height(); }
  Framebuffer& framebuffer() override { return fb_; }

  void flush(RefreshMode mode) override;

  void set_frontlight(int level) override;
  int frontlight() const override { return frontlight_; }

  uint32_t last_flush_ms() const { return last_flush_ms_; }

 private:
  void blit();

  Inkplate* panel_;
  Framebuffer fb_;  // 776 KB; must be allocated before fonts and edition
  int frontlight_ = 0;
  uint32_t last_flush_ms_ = 0;
};

}  // namespace device
}  // namespace rsspaper
```

- [ ] **Step 2: Write the implementation**

`writePixelInternal` is used rather than `drawPixel` because it skips Adafruit_GFX rotation and bounds work; the spike measured `drawPixel` at 749 ms for a full frame, which is a third of a page turn.

```cpp
#include "device/device_display.h"

namespace rsspaper {
namespace device {

void DeviceDisplay::blit() {
  const uint8_t* px = fb_.pixels();
  const int w = fb_.width();
  const int h = fb_.height();
  for (int y = 0; y < h; ++y) {
    const uint8_t* row = px + static_cast<size_t>(y) * w;
    for (int x = 0; x < w; ++x) {
      panel_->writePixelInternal(x, y, row[x] >> 5);
    }
  }
}

void DeviceDisplay::flush(RefreshMode mode) {
  const uint32_t t0 = millis();
  blit();
  switch (mode) {
    case RefreshMode::Partial:
      panel_->partialUpdate(false, false);
      break;
    case RefreshMode::Full:
      panel_->display();
      break;
    case RefreshMode::DeepClean:
      panel_->burnInClean(2, 20);
      panel_->display();
      break;
  }
  last_flush_ms_ = millis() - t0;
}

void DeviceDisplay::set_frontlight(int level) {
  frontlight_ = level < 0 ? 0 : (level > max_frontlight() ? max_frontlight() : level);
  panel_->setState(frontlight_ > 0);
  panel_->setBrightness(static_cast<uint8_t>(frontlight_));
}

}  // namespace device
}  // namespace rsspaper
```

- [ ] **Step 3: Exercise it from `main.cpp`**

After the storage smoke test, render a test pattern and time each mode:

```cpp
  static device::DeviceDisplay display(&panel);
  Framebuffer& fb = display.framebuffer();
  fb.fill(kPaper);
  fb.frame_rect(40, 40, fb.width() - 80, fb.height() - 80, kInk);
  fb.fill_rect(100, 100, 200, 100, 96);
  display.flush(RefreshMode::Full);
  Serial.printf("full    %u ms\n", (unsigned)display.last_flush_ms());
  fb.fill_rect(100, 300, 200, 100, 32);
  display.flush(RefreshMode::Partial);
  Serial.printf("partial %u ms\n", (unsigned)display.last_flush_ms());
```

- [ ] **Step 4: Verify on device**

Run: `cd src/device && pio run -t upload && pio device monitor`
Expected: a framed page with two grey blocks. Timings printed. **The full-refresh number must be under 1900 ms and the partial under 500 ms.** If the partial is over 500 ms the blit is still the bottleneck — see Step 5.

- [ ] **Step 5: If the blit is still too slow, reach the panel buffer directly**

`DMemory4Bit` is protected on the driver, so a derived class can reach it. Only do this if Step 4 missed its budget. Add to `device_display.h`:

```cpp
// Reaches the 3-bit panel buffer directly. Two pixels per byte, high nibble
// first. Only needed because per-pixel calls cost a third of a page turn.
class PanelAccess : public Inkplate {
 public:
  uint8_t* buffer3bit() { return DMemory4Bit; }
};
```

and replace `blit()`'s inner loop with a two-pixels-per-byte pack. Re-run Step 4.

- [ ] **Step 6: Commit**

```bash
git add src/device/device_display.h src/device/device_display.cpp src/device/main.cpp
git commit -m "Implement IDisplay with a direct panel blit"
```

---

### Task 4: DeviceInput, DeviceClock, DevicePower, DeviceHttpClient

**Files:**
- Create: `src/device/device_input.h`, `src/device/device_input.cpp`
- Create: `src/device/device_clock.h`, `src/device/device_clock.cpp`
- Create: `src/device/device_power.h`, `src/device/device_power.cpp`
- Create: `src/device/device_http.h`
- Modify: `src/device/main.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces: `DeviceInput(Inkplate*)` with `bool begin()`; `DeviceClock(Inkplate*)` with `void set_utc_offset(int seconds)`; `DevicePower(Inkplate*)` with `void set_storage(DeviceStorage*)` so it can put the card to sleep before `esp_deep_sleep_start`; `DeviceHttpClient` returning status 0.

- [ ] **Step 1: Write `device_input.h` / `.cpp`**

```cpp
#pragma once

#include <cstddef>
#include <cstdint>

#include "Inkplate.h"
#include "hal/hal.h"

namespace rsspaper {
namespace device {

class DeviceInput final : public IInput {
 public:
  explicit DeviceInput(Inkplate* panel) : panel_(panel) {}
  bool begin() { return panel_->touchscreen.init(true); }

  size_t poll(TouchPoint* out, size_t max) override;
  uint32_t millis() const override { return ::millis(); }

 private:
  Inkplate* panel_;
};

}  // namespace device
}  // namespace rsspaper
```

```cpp
#include "device/device_input.h"

namespace rsspaper {
namespace device {

size_t DeviceInput::poll(TouchPoint* out, size_t max) {
  if (max == 0 || !panel_->touchscreen.available()) return 0;
  uint16_t xs[2] = {0, 0};
  uint16_t ys[2] = {0, 0};
  const uint8_t n = panel_->touchscreen.getData(xs, ys);
  const size_t count = n < max ? n : max;
  for (size_t i = 0; i < count; ++i) {
    out[i].x = xs[i];
    out[i].y = ys[i];
  }
  return count;
}

}  // namespace device
}  // namespace rsspaper
```

- [ ] **Step 2: Write `device_clock.h` / `.cpp`**

```cpp
#pragma once

#include "Inkplate.h"
#include "core/base/datetime.h"
#include "hal/hal.h"

namespace rsspaper {
namespace device {

class DeviceClock final : public IClock {
 public:
  explicit DeviceClock(Inkplate* panel) : panel_(panel) {}

  // From feeds.toml, never from the network.
  void set_utc_offset(int seconds) { offset_ = seconds; }

  Epoch now() const override;
  int utc_offset_seconds() const override { return offset_; }
  void set_wake_alarm(Epoch when) override;

 private:
  Inkplate* panel_;
  int offset_ = 0;
};

}  // namespace device
}  // namespace rsspaper
```

```cpp
#include "device/device_clock.h"

namespace rsspaper {
namespace device {

Epoch DeviceClock::now() const {
  return static_cast<Epoch>(panel_->getEpoch());
}

void DeviceClock::set_wake_alarm(Epoch when) {
  if (when == kNoDate) return;
  // match on second+minute+hour+day
  panel_->setAlarmEpoch(static_cast<uint32_t>(when), 15);
}

}  // namespace device
}  // namespace rsspaper
```

- [ ] **Step 3: Write `device_power.h` / `.cpp`**

```cpp
#pragma once

#include "Inkplate.h"
#include "core/base/datetime.h"
#include "device/device_storage.h"
#include "hal/hal.h"

namespace rsspaper {
namespace device {

class DevicePower final : public IPower {
 public:
  explicit DevicePower(Inkplate* panel) : panel_(panel) {}

  // So the card's SPI pins can be floated before sleeping.
  void set_storage(DeviceStorage* storage) { storage_ = storage; }

  void deep_sleep_until(Epoch when) override;
  int battery_millivolts() const override;
  bool on_external_power() const override { return panel_->isPowerGood(); }

 private:
  Inkplate* panel_;
  DeviceStorage* storage_ = nullptr;
};

}  // namespace device
}  // namespace rsspaper
```

```cpp
#include "device/device_power.h"

#include <esp_sleep.h>

namespace rsspaper {
namespace device {

int DevicePower::battery_millivolts() const {
  return static_cast<int>(panel_->readBattery() * 1000.0);
}

void DevicePower::deep_sleep_until(Epoch when) {
  (void)when;  // the alarm is armed through IClock::set_wake_alarm
  panel_->einkOff();
  if (storage_ != nullptr) panel_->sdCardSleep();
  // Two wake sources, both active low: GPIO 39 is the RTC alarm interrupt
  // (a compose wake) and GPIO 36 is the wake button (a read wake). ext1
  // rather than ext0 because ext0 takes only one pin, and a reader that can
  // only be woken by the daily alarm is not a reader.
  esp_sleep_enable_ext1_wakeup((1ULL << GPIO_NUM_39) | (1ULL << GPIO_NUM_36),
                               ESP_EXT1_WAKEUP_ALL_LOW);
  esp_deep_sleep_start();  // does not return
}

}  // namespace device
}  // namespace rsspaper
```

- [ ] **Step 4: Write `device_http.h`**

```cpp
// A stub until issue #3. It exists so Hal::complete() holds and the colophon
// can say why there is no network rather than the reader wondering.
#pragma once

#include <memory>

#include "hal/hal.h"

namespace rsspaper {
namespace device {

class DeviceHttpClient final : public IHttpClient {
 public:
  std::unique_ptr<ByteSource> get(const HttpRequest& request,
                                  HttpResponse* out) override {
    (void)request;
    if (out != nullptr) {
      out->status = 0;
      out->error = "no network yet";
    }
    return nullptr;
  }
};

}  // namespace device
}  // namespace rsspaper
```

- [ ] **Step 5: Exercise from `main.cpp`**

```cpp
  static device::DeviceInput input(&panel);
  static device::DeviceClock clock_(&panel);
  static device::DevicePower power(&panel);
  static device::DeviceHttpClient http;
  power.set_storage(&storage);
  Serial.printf("touch init: %d\n", input.begin());
  Serial.printf("rtc epoch: %ld\n", (long)clock_.now());
  Serial.printf("battery: %d mV, external: %d\n", power.battery_millivolts(),
                (int)power.on_external_power());
  for (int i = 0; i < 40; ++i) {
    TouchPoint p[2];
    const size_t n = input.poll(p, 2);
    if (n > 0) Serial.printf("touch %u at %d,%d\n", (unsigned)n, p[0].x, p[0].y);
    delay(100);
  }
```

- [ ] **Step 6: Verify on device**

Run: `cd src/device && pio run -t upload && pio device monitor`
Expected: `touch init: 1`, a plausible epoch, a battery reading between 3000 and 4300 mV, and touch coordinates printed within 1024x758 as you touch the panel.

- [ ] **Step 7: Commit**

```bash
git add src/device
git commit -m "Implement input, clock, power and an HTTP stub"
```

---

### Task 5: Portable idle tiering

**Files:**
- Create: `src/core/ui/session.h`, `src/core/ui/session.cpp`
- Create: `test/session_test.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces: `enum class SessionIntent { Stay, Doze, Sleep }` and `class Session` with `Session(SessionThresholds)`, `void touched(uint32_t now_ms)`, `SessionIntent intent(uint32_t now_ms) const`. `struct SessionThresholds { uint32_t doze_after_ms = 30000; uint32_t sleep_after_ms = 300000; }`.

This is the only task with real unit tests — the HAL cannot be tested off-device, so the policy is deliberately portable so that it can be.

- [ ] **Step 1: Write the failing test**

`test/session_test.cpp`:

```cpp
#include "core/ui/session.h"

#include "doctest.h"

using namespace rsspaper;

TEST_CASE("session stays awake immediately after a touch") {
  Session s{SessionThresholds{}};
  s.touched(1000);
  CHECK(s.intent(1000) == SessionIntent::Stay);
  CHECK(s.intent(29000) == SessionIntent::Stay);
}

TEST_CASE("session dozes once the doze threshold passes") {
  Session s{SessionThresholds{}};
  s.touched(1000);
  CHECK(s.intent(31001) == SessionIntent::Doze);
}

TEST_CASE("session sleeps once the sleep threshold passes") {
  Session s{SessionThresholds{}};
  s.touched(1000);
  CHECK(s.intent(301001) == SessionIntent::Sleep);
}

TEST_CASE("a touch resets the idle clock") {
  Session s{SessionThresholds{}};
  s.touched(1000);
  CHECK(s.intent(31001) == SessionIntent::Doze);
  s.touched(31001);
  CHECK(s.intent(31002) == SessionIntent::Stay);
}

TEST_CASE("millis rollover does not strand the reader asleep") {
  Session s{SessionThresholds{}};
  s.touched(0xFFFFF000u);
  // 0x1000 ms later, having wrapped past zero.
  CHECK(s.intent(0x00000000u + 0x0FFFu) == SessionIntent::Stay);
}
```

- [ ] **Step 2: Run it and watch it fail**

Run: `make tests && ./bin/rsspaper-tests --test-case="*session*"`
Expected: FAIL — `core/ui/session.h` does not exist.

- [ ] **Step 3: Write the implementation**

`src/core/ui/session.h`:

```cpp
// How long the reader stays resident after the last touch.
//
// This is policy, and it lives in portable code rather than in src/device/ so
// that it can be tested against SimPower on a laptop. Sleep behaviour that
// only exists on the device is debuggable only on the device.
#pragma once

#include <cstdint>

namespace rsspaper {

struct SessionThresholds {
  // Rebuilding state from the card costs ~2.25 s, a page turn with state
  // resident ~0.4 s. Staying resident briefly is what makes picking the paper
  // back up feel like paper. Real values belong to issue #5.
  uint32_t doze_after_ms = 30000;
  uint32_t sleep_after_ms = 300000;
};

enum class SessionIntent { Stay, Doze, Sleep };

class Session {
 public:
  explicit Session(SessionThresholds thresholds) : t_(thresholds) {}

  void touched(uint32_t now_ms) { last_ = now_ms; }
  SessionIntent intent(uint32_t now_ms) const;

 private:
  SessionThresholds t_;
  uint32_t last_ = 0;
};

}  // namespace rsspaper
```

`src/core/ui/session.cpp`:

```cpp
#include "core/ui/session.h"

namespace rsspaper {

SessionIntent Session::intent(uint32_t now_ms) const {
  // Unsigned subtraction wraps, which is exactly right: millis() rolls over
  // every 49 days and a reader must not be stranded asleep when it does.
  const uint32_t idle = now_ms - last_;
  if (idle >= t_.sleep_after_ms) return SessionIntent::Sleep;
  if (idle >= t_.doze_after_ms) return SessionIntent::Doze;
  return SessionIntent::Stay;
}

}  // namespace rsspaper
```

- [ ] **Step 4: Run the tests**

Run: `make tests && ./bin/rsspaper-tests --test-case="*session*"`
Expected: PASS, 5 test cases.

- [ ] **Step 5: Run the whole suite and the portability gate**

Run: `make check`
Expected: 160 tests pass, `portability: src/core and src/hal are clean`.

- [ ] **Step 6: Commit**

```bash
git add src/core/ui/session.h src/core/ui/session.cpp test/session_test.cpp
git commit -m "Add portable idle tiering for the reading session"
```

---

### Task 6: The composition root and the read lifecycle

**Files:**
- Modify: `src/device/main.cpp` (replace entirely)
- Create: `src/device/device_hal.h`

**Interfaces:**
- Consumes: everything from Tasks 2–5.
- Produces: a device that reads. `struct DeviceHal` aggregating the six implementations, with `bool begin()`.

- [ ] **Step 1: Write `device_hal.h`**

```cpp
// Everything the firmware needs from the world, assembled. Mirrors the shape
// of src/sim/sim_hal.h so the two stay readable against each other.
#pragma once

#include "Inkplate.h"
#include "device/device_clock.h"
#include "device/device_display.h"
#include "device/device_http.h"
#include "device/device_input.h"
#include "device/device_power.h"
#include "device/device_storage.h"
#include "hal/hal.h"

namespace rsspaper {
namespace device {

struct DeviceHal {
  explicit DeviceHal(Inkplate* panel)
      : display(panel), input(panel), clock(panel), power(panel),
        storage(panel) {}

  // Order matters: the display's 776 KB framebuffer is claimed on
  // construction, before the font pack and edition fragment PSRAM.
  DeviceDisplay display;
  DeviceInput input;
  DeviceClock clock;
  DevicePower power;
  DeviceStorage storage;
  DeviceHttpClient http;

  Hal as_hal() {
    Hal hal;
    hal.display = &display;
    hal.input = &input;
    hal.clock = &clock;
    hal.power = &power;
    hal.storage = &storage;
    hal.http = &http;
    return hal;
  }
};

}  // namespace device
}  // namespace rsspaper
```

- [ ] **Step 2: Write the read lifecycle in `main.cpp`**

```cpp
// The device composition root: pick a lifecycle from the wake reason, build
// the HAL, run. No policy lives here — that is what src/core/ is for.
#include <Arduino.h>
#include <esp_sleep.h>

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
device::DeviceHal* hal_impl = nullptr;
FontPack fonts;
Edition edition;
Reader* reader = nullptr;
Session session{SessionThresholds{}};
GestureRecognizer gestures;

bool load_edition(device::DeviceHal& d) {
  std::string blob;
  if (!d.storage.read("/literata.rfp", &blob)) return false;
  std::vector<uint8_t> bytes(blob.begin(), blob.end());
  blob.clear();
  blob.shrink_to_fit();
  std::string err;
  if (!fonts.load(std::move(bytes), &err)) return false;
  if (!d.storage.read("/edition.rspe", &blob)) return false;
  return deserialize_edition(blob, &edition, &err);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(300);
  panel.begin();

  static device::DeviceHal d(&panel);  // claims the framebuffer first
  hal_impl = &d;

  if (!d.storage.mount()) {
    Serial.println("no card");
    return;  // Task 7 renders a page here
  }
  if (!load_edition(d)) {
    Serial.println("no edition");
    return;  // Task 7 renders a page here
  }
  d.input.begin();

  Hal hal = d.as_hal();
  static Reader r(edition, fonts, hal);
  reader = &r;
  reader->load_clippings("clippings.dat");
  reader->render();
  session.touched(millis());
  Serial.printf("reading: %u pages\n", (unsigned)edition.pages.size());
}

void loop() {
  if (reader == nullptr) { delay(1000); return; }

  TouchPoint p[2];
  const size_t n = hal_impl->input.poll(p, 2);
  const uint32_t now = millis();
  const GestureEvent e =
      n > 0 ? gestures.update(true, p[0].x, p[0].y, now)
            : gestures.update(false, 0, 0, now);
  if (e.kind != Gesture::None) {
    session.touched(now);
    reader->handle(e);
    reader->tick();
  }

  if (session.intent(now) == SessionIntent::Sleep) {
    Serial.println("sleeping");
    hal_impl->power.deep_sleep_until(kNoDate);
  }
  delay(20);
}
```

- [ ] **Step 3: Verify on device**

Run: `cd src/device && pio run -t upload && pio device monitor`
Expected: `reading: 168 pages`, the front page on the panel, and swiping turns pages through the real gesture recogniser. After 5 minutes idle: `sleeping`, then the panel holds its image at deep-sleep current.

- [ ] **Step 4: Verify the gesture path matches the simulator**

Run: `make read` on the desktop and compare that a swipe left advances a page in both.
Expected: same navigation behaviour; the device is running the same `Reader`.

- [ ] **Step 5: Commit**

```bash
git add src/device
git commit -m "Drive the real Reader from the device HAL"
```

---

### Task 7: Failure pages and feeds.toml from the card

**Files:**
- Create: `src/core/ui/notice.h`, `src/core/ui/notice.cpp`
- Create: `test/notice_test.cpp`
- Modify: `src/device/main.cpp`

**Interfaces:**
- Consumes: `Framebuffer`, `FontPack`.
- Produces: `void render_notice(const FontPack&, const char* headline, const char* body, Framebuffer*)`.

Closes #15 and delivers spec decision D5.

- [ ] **Step 1: Write the failing test**

`test/notice_test.cpp`:

```cpp
#include "core/ui/notice.h"

#include "core/render/framebuffer.h"
#include "core/text/font_pack.h"
#include "doctest.h"

using namespace rsspaper;

TEST_CASE("a notice marks the page without a font pack") {
  FontPack empty;
  Framebuffer fb;
  fb.fill(kPaper);
  render_notice(empty, "No card", "Insert a card.", &fb);
  // With no faces available it must still not leave a blank sheet.
  bool any_ink = false;
  for (int y = 0; y < fb.height() && !any_ink; ++y)
    for (int x = 0; x < fb.width(); ++x)
      if (fb.get(x, y) != kPaper) { any_ink = true; break; }
  CHECK(any_ink);
}
```

- [ ] **Step 2: Run it and watch it fail**

Run: `make tests && ./bin/rsspaper-tests --test-case="*notice*"`
Expected: FAIL — `core/ui/notice.h` does not exist.

- [ ] **Step 3: Write the implementation**

`src/core/ui/notice.h`:

```cpp
// The page the reader gets when there is nothing to read: no card, an
// unreadable card, no font pack. A notice is a page, not an error — the first
// card this project ever used was unreadable.
#pragma once

#include "core/render/framebuffer.h"
#include "core/text/font_pack.h"

namespace rsspaper {

void render_notice(const FontPack& fonts, const char* headline,
                   const char* body, Framebuffer* fb);

}  // namespace rsspaper
```

`src/core/ui/notice.cpp`:

```cpp
#include "core/ui/notice.h"

#include "core/layout/page.h"
#include "core/text/faces.h"

namespace rsspaper {

void render_notice(const FontPack& fonts, const char* headline,
                   const char* body, Framebuffer* fb) {
  fb->fill(kPaper);

  // A rule, so the page reads as deliberate even when no face loaded.
  fb->fill_rect(kSideMargin, 120, kPageWidth - 2 * kSideMargin, 3, kInk);

  const Face& head = fonts.face(FaceId::Head);
  const Face& text = fonts.face(FaceId::Body);
  if (head.valid()) {
    fb->draw_text(head, headline, kSideMargin * kSubpixel, 100, kInk);
  }
  if (text.valid()) {
    fb->draw_text(text, body, kSideMargin * kSubpixel, 180, kInk);
  }
}

}  // namespace rsspaper
```

- [ ] **Step 4: Run the tests**

Run: `make check`
Expected: PASS, 161 tests.

- [ ] **Step 5: Wire the notices and feeds.toml into `main.cpp`**

Replace the two `return;` placeholders from Task 6:

```cpp
  if (!d.storage.mount()) {
    const bool unreadable = d.storage.state() == device::CardState::Unreadable;
    render_notice(fonts, unreadable ? "Card unreadable" : "No card",
                  unreadable
                      ? "The card is readable but its filesystem is not. Format it as exFAT."
                      : "Insert a card with feeds.toml on it.",
                  &d.display.framebuffer());
    d.display.flush(RefreshMode::Full);
    return;
  }
```

and after a successful mount, read the config from the card so #15 is closed:

```cpp
  FeedList feeds;
  std::string toml;
  std::string cfg_error;
  if (d.storage.read("/feeds.toml", &toml) &&
      parse_feeds_toml(toml, &feeds, &cfg_error)) {
    d.clock.set_utc_offset(feeds.edition.utc_offset_minutes * 60);
  }
```

`utc_offset_minutes` does not exist yet — Step 5a adds it.

- [ ] **Step 5a: Add the UTC offset to the config, test first**

The masthead date and `wake_at` are local times, and the device has no network
to ask. `EditionConfig` has no offset field today.

Add to `test/opml_test.cpp`'s neighbour `test/feeds_config_test.cpp` (create it
if absent):

```cpp
TEST_CASE("edition carries a utc offset in minutes") {
  FeedList list;
  std::string error;
  REQUIRE(parse_feeds_toml("[edition]\nutc_offset_minutes = -300\n", &list,
                           &error));
  CHECK(list.edition.utc_offset_minutes == -300);
}
```

Run: `make tests && ./bin/rsspaper-tests --test-case="*utc offset*"` — expect FAIL.

Add to `EditionConfig` in `src/core/config/feeds_config.h`, beside `wake_at`:

```cpp
  // Minutes east of UTC. The device has no network to ask, and the masthead
  // date and wake_at are both local.
  int utc_offset_minutes = 0;
```

and parse it in `src/core/config/feeds_config.cpp` alongside the other
`[edition]` integer keys, following the pattern already used for
`max_age_days`.

Run the same test — expect PASS. Then `make check`.

- [ ] **Step 6: Verify on device**

Run: `cd src/device && pio run -t upload`, then pull the card and reset.
Expected: a typeset "No card" page on the panel, not a blank screen and not a crash. Reinsert, reset, and the paper comes back.

- [ ] **Step 7: Commit**

```bash
git add src/core/ui/notice.h src/core/ui/notice.cpp test/notice_test.cpp src/device/main.cpp
git commit -m "Render a page when there is nothing to read, and read feeds.toml from the card"
```

---

### Task 8: The compose lifecycle and CI

**Files:**
- Modify: `src/device/main.cpp`
- Modify: `.github/workflows/` (the existing CI workflow file)

**Interfaces:**
- Consumes: everything above.
- Produces: `enum class Lifecycle { Compose, Read }` and `Lifecycle lifecycle_from_wake()`.

- [ ] **Step 1: Add the lifecycle branch**

```cpp
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
```

and in `setup()`, before building the Reader:

```cpp
  if (lifecycle_from_wake() == Lifecycle::Compose) {
    // Never constructs a Reader and never holds an Edition while the radio is
    // up: that separation is what keeps issue #3 inside the internal-RAM
    // budget. Until #3 lands there is nothing to fetch, so re-arm and sleep.
    Serial.println("compose wake: no network yet");
    d.clock.set_wake_alarm(d.clock.now() + 24 * 60 * 60);
    d.power.deep_sleep_until(kNoDate);
  }
```

- [ ] **Step 2: Verify on device**

Run: `cd src/device && pio run -t upload && pio device monitor`
Expected: on a normal reset, `reading: 168 pages`. The compose branch cannot be exercised until an RTC alarm fires; confirm by temporarily setting the alarm 60 s out and watching for `compose wake` after it sleeps.

- [ ] **Step 3: Add the device build to CI**

Add a job to the existing workflow. Issue #4 asks for this explicitly — compiling in CI is what catches `hal.h` drifting away from its implementations.

```yaml
  device:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - uses: actions/setup-python@v5
        with:
          python-version: '3.12'
      - run: pip install platformio
      - run: cd src/device && pio run
```

- [ ] **Step 4: Verify CI locally**

Run: `cd src/device && pio run`
Expected: SUCCESS.

- [ ] **Step 5: Commit**

```bash
git add src/device .github/workflows
git commit -m "Branch the firmware on wake reason and build it in CI"
```

---

### Task 9: Correct the documentation

**Files:**
- Modify: `README.md`
- Modify: `src/hal/hal.h`
- Modify: `DECISIONS.md`

- [ ] **Step 1: Fix the PSRAM and refresh figures**

In `README.md`, the hardware paragraph: `8 MB PSRAM` becomes `8 MB PSRAM (4 MB addressable — plain ESP32 maps only 4 MB)`, and `~1.26 s 3-bit greyscale full refresh` becomes `~1.74 s`. Fix the same refresh figure in the `RefreshMode` comment in `src/hal/hal.h`.

- [ ] **Step 2: Update the status table**

In `README.md`, `Inkplate 6FLICK target` moves from `not started` to `working — reads an edition off the card by touch`.

- [ ] **Step 3: Add the decisions**

Append to `DECISIONS.md`:

```markdown
### 26. Storage is the microSD card, not flash

The board has 4 MB of flash, not the 16 MB assumed. A font pack (733 KB) and a
composed edition (360 KB) in a flash partition would have consumed most of it
and permanently forfeited OTA. So `IStorage` maps to the card and flash holds
firmware only.

This also settles #15: `feeds.toml` is edited on a computer and carried on the
card, which is the option that issue itself called the most honest about what
the product is. No captive portal, no QR handshake, no server.

The costs are real. A card is required hardware; SD reads are slower than
flash (984 ms against 567 ms for the font pack, paid on every cold wake); and
a bad card is a normal state rather than a hypothetical — the first card used
here was HFS+ and unreadable while answering perfectly at block level, which is
why an unreadable card renders a page rather than failing.

### 27. Two phase-separated firmware lifecycles

A resident `Edition` costs ~221 KB of internal RAM, because deserialising it
makes thousands of small allocations and the Arduino allocator forces anything
under ~16 KB into internal RAM rather than PSRAM. That leaves ~69 KB, which is
less than WiFi and mbedTLS need.

So a compose wake never constructs a `Reader`, and a read wake never brings up
the radio. The two heavy consumers are separated by a deep sleep, which makes
the budget work by construction rather than by careful sequencing.

### 28. The Inkplate library is LGPL-3.0, and stays below the HAL

Every other dependency here is public domain, MIT or OFL. The Soldered Inkplate
library is LGPL-3.0. It is confined to `src/device/`, so the portable core is
unaffected and a reimplementation against another panel inherits nothing — but
distributing firmware binaries carries a relinking obligation, and that is a
deliberate acceptance rather than an oversight.
```

- [ ] **Step 4: Verify**

Run: `make check`
Expected: unchanged pass. Re-read the status table against what actually works.

- [ ] **Step 5: Commit**

```bash
git add README.md src/hal/hal.h DECISIONS.md
git commit -m "Correct the hardware figures against measurements"
```

---

## Plans that follow

Each is its own plan against the same spec, written when its predecessor lands:

- **Plan 2 — HTTP fetcher (#3).** Unblocked by Task 8's compose lifecycle. Needs wifi credentials on the card to test.
- **Plan 3 — Furniture (#8 frontlight, #16 battery mark).** Both are thin once the HAL exists; #16's threshold needs a measured discharge.
- **Plan 4 — Refresh and power policy (#7, #5).** Both are measurement-led: ghosting needs eyes on the panel, weeks-per-charge needs a 24-hour draw.
