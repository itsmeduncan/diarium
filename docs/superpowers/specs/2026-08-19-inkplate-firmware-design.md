# Inkplate 6FLICK firmware: design

Issue #4 in detail, and how the rest of the 1.0.0 milestone sits on it.
Written 2026-08-19, after a hardware spike whose measurements are the reason
several of the original issue descriptions no longer hold.

## What the spike established

A throwaway PlatformIO project loaded the real font pack and a real composed
edition off the device, rendered page 0 through `PageRenderer`, and put it on
the panel.

**The portable core cross-compiled for xtensa and ran with zero source
changes** — all 25 translation units in `src/core/`, including `reader.cpp`,
`feed_parser.cpp` and `hyphenator.cpp`. The rule in `hal.h` about platform
headers is what made that true, and it should be defended accordingly.

### Measured constraints

These are the numbers the design has to satisfy. All from an ESP32-D0WD-V3
rev 3.1 on a 6FLICK, Arduino core 2.0.17.

| Quantity | Measured | Was assumed |
| --- | --- | --- |
| Flash | 4 MB | 16 MB implied by the partition sketch |
| Usable PSRAM | ~4 MB (3.38 MB free after Inkplate's buffers) | 8 MB |
| Internal RAM free with an Edition resident | 69 KB (largest block 63 KB) | not considered |
| Full 3-bit refresh | 1738 ms | ~1260 ms |
| `PageRenderer::render`, one page | 201–217 ms | — |
| Font pack read, SD (exFAT) | 984 ms | — |
| Edition read, SD | 485 ms | — |
| `deserialize_edition` | 783 ms (CPU-bound, storage-independent) | — |
| SD sustained read | 762 KB/s | — |

Three of these drive everything below.

**Internal RAM is the scarce resource, not PSRAM.** A resident `Edition` costs
~221 KB of *internal* RAM, because `deserialize_edition` makes thousands of
small allocations and the Arduino allocator forces anything under ~16 KB into
internal RAM. WiFi plus mbedTLS needs more than the 69 KB that leaves. **The
network stack and a resident Edition cannot coexist.**

**Flash is the scarce persistent resource.** 4 MB total. Putting the font pack
and edition in a flash partition would forfeit OTA permanently.

**Cold wake costs ~2.25 s** before a pixel can be drawn (984 + 485 + 783), of
which 783 ms is CPU that no storage decision can improve.

### Corrections owed to the docs

- `README.md` claims 8 MB PSRAM; plain ESP32 maps only 4 MB regardless of what
  is fitted.
- `README.md` and `hal.h` claim a ~1.26 s full refresh; it is ~1.74 s.
- Issue #17 claimed swapping the body face was a one-line change; the Medium
  weight is not vendored. Resolved: Regular stays, checked on the panel.

## Decisions

### D1. The microSD card is the storage substrate; flash holds only firmware

`IStorage` maps to the card. This buys back ~1.75 MB of flash — the scarce
resource — and keeps OTA possible.

It also decides #15 for free: `feeds.toml` is edited on a computer and carried
on the card. That is the option #15 itself calls "the most honest about what
the product is", and it needs no captive portal, no QR handshake, and no
server.

Cost, accepted: the card is required hardware, SD reads are slower than flash
(984 ms vs 567 ms for the font pack), and a bad card is a real failure mode —
see D5. The 0.65 s is the price of the flash headroom.

### D2. `src/device/` holds HAL implementations and a composition root, nothing else

```
src/device/
  main.cpp              pick lifecycle by wake reason, build Hal, run
  device_display.cpp    IDisplay
  device_input.cpp      IInput
  device_clock.cpp      IClock
  device_power.cpp      IPower
  device_storage.cpp    IStorage
  device_http.cpp       IHttpClient  (stub until #3)
```

One file per interface, mirroring `src/sim/sim_hal.h`, so the two
implementations stay readable against each other. No policy lives here.

### D3. Two phase-separated lifecycles

`main.cpp` branches on `esp_sleep_get_wakeup_cause()`:

- **Compose wake** (RTC alarm) — build Hal, fetch, compose, persist, re-arm the
  alarm, deep sleep. **Never constructs a `Reader` and never holds an Edition
  while the radio is up.**
- **Read wake** (touch) — build Hal, load font pack and edition from the card,
  construct `Reader`, serve gestures, sleep on idle. **Never brings up WiFi.**

This is what makes the internal-RAM budget work: the two heavy consumers are
separated by a deep sleep, so they never coexist by construction rather than by
careful sequencing.

In #4 `IHttpClient` is a stub, so a compose wake only re-arms the alarm. The
branch is built now so #3 drops in without re-architecting.

### D4. Power tiering is portable code, not a device state machine

A new `src/core/ui/session.h`. `Session` owns the idle clock and yields an
intent — `Stay`, `Doze`, `Sleep` — which `Reader` acts on through `IPower`.
Thresholds are data.

The reason this is not in `src/device/` is testability: `SimPower` already
records `slept_until()`, so the simulator can advance a clock and assert the
transitions. Sleep behaviour that lives only in device code is debuggable only
on hardware, which is the one place it is most painful to debug.

Tiering follows from the measurements: stay resident briefly after the last
touch, so a page turn during a reading session costs ~0.4 s rather than
~2.7 s; drop to deep sleep after a longer idle and accept the ~2.25 s rebuild.
The thresholds themselves are #5's to measure.

### D5. An unreadable card is a page, not a crash

The first card inserted during the spike was HFS+ formatted and SdFat could not
read it, while the card answered perfectly at block level. This is a normal
state, not an exceptional one.

`IStorage` reports failure rather than hanging. The composition root renders a
plain "no card" page through the same `PageRenderer`, and offers a
format-on-confirm path (SdFat's `SdFs::format()` works from the device, and the
spike exercised it). Same treatment for a missing or corrupt font pack.

### D6. The 8-bit to 3-bit reduction writes the panel buffer directly

The spike's per-pixel `drawPixel` loop costs 749 ms, roughly a third of a page
turn, and must not survive into `IDisplay::flush`.

## Interface notes

**`IDisplay`** — framebuffer allocated in PSRAM. No allocator seam is needed:
allocations over ~16 KB go to PSRAM transparently. **Allocation order matters**
— claim the 776 KB framebuffer before the font pack and edition fragment PSRAM,
or `operator new` throws with 1.6 MB still free. `flush` maps `RefreshMode` to
`partialUpdate()` / `display()` / a deep-clean sequence. Note the library's own
`_partialUpdateLimiter = 10` forces a full refresh every 10 partials; the
policy above the HAL either matches that or overrides it deliberately (#7).

**`IInput`** — Cypress controller via `touchscreen.init/available/getData`.
Release coordinates stay ignored per DECISIONS #21.

**`IClock`** — PCF85063 RTC. `utc_offset_seconds` comes from `feeds.toml`, not
from the network.

**`IPower`** — `deep_sleep_until` arms the RTC alarm and calls
`esp_deep_sleep_start`. `sdCardSleep()` must run first; it floats the SPI pins
to cut deep-sleep draw. Battery millivolts from the ADC.

**`IStorage`** — SdFat over exFAT. Paths are flat.

**`IHttpClient`** — returns status 0 with a human-readable error until #3, so
`Hal::complete()` holds and the colophon can say the network is not built yet.

## How the rest of 1.0.0 sits on this

- **#3 HTTP fetcher** — unblocked by D3, which is the only reason it fits. The
  fetch path must stream into the parser and never hold a full Edition. PSRAM
  is not the constraint on a compose wake — no framebuffer is allocated on that
  path — but the ~69 KB internal budget is, and it is unmeasured against
  mbedTLS. Measure that high-water mark before committing to a design.
- **#5 Power loop** — reduced to policy and measurement, since #4 now provides
  real sleep and wake. Its job is choosing `Session`'s thresholds and defending
  the weeks-per-charge claim with a measured 24-hour draw.
- **#7 Refresh policy** — `Reader::choose_refresh` already exists; #4 only
  executes it. Needs re-baselining against 1738 ms rather than 1260 ms, which
  makes a full refresh ~7.7x a partial, not ~5.6x. Ghosting is still unmeasured.
- **#8 Frontlight** — `set_frontlight` is already in `IDisplay`. Level persists
  via `IStorage`, not RTC memory, so it survives a battery pull.
- **#15 Feeds onto the device** — answered by D1. Closeable once `feeds.toml`
  is read from the card.
- **#16 Battery in the furniture** — `IPower::battery_millivolts` feeds the
  folio. A discreet mark when low, per the issue. Needs a millivolt threshold
  measured against the actual cell, not a percentage.

## Testing

The six implementations cannot be unit-tested off-device, so the portable side
carries the weight:

- `Session` gets simulator tests against `SimPower::slept_until()`.
- CI compiles the device target, as #4 asks, to catch drift between `hal.h` and
  its implementations.
- `make check` and `tools/check-portability.sh` are untouched, so the
  portability gate keeps running exactly as it does now.
- A `make device` target invokes PlatformIO. CMake stays authoritative for the
  desktop build.

## Deliberately not in scope

- OTA. Possible under D1 but not built.
- Any composition on the read path. An edition is composed at a compose wake.
- Reducing the 783 ms `deserialize_edition` cost. It is the largest single
  lever on wake latency and it is portable, testable work — but it is its own
  change, not part of the port.

## Open questions

- `Session`'s thresholds are unmeasured; #5 owns them.
- Deep-sleep current with the card fitted is unmeasured. The ~23 µA figure in
  the README predates the card, and `sdCardSleep()` exists precisely because
  the card costs something.
- The Inkplate library is LGPL-3.0 against an MIT repo. It sits below
  `src/hal/`, so the portable core is unaffected, but distributing firmware
  binaries carries a relinking obligation. Wants a `DECISIONS.md` entry.
