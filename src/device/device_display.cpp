#include "device/device_display.h"

#include <cstring>

#include "core/render/reduce.h"

namespace rsspaper {
namespace device {
namespace {

// The panel's own buffer layouts, written a byte at a time rather than a
// pixel at a time. Per-pixel calls through the library cost 540-750 ms for a
// frame, which is a third of a page turn spent on packing.
//
// 3-bit: DMemory4Bit, two pixels per byte, even x in the high nibble.
// 1-bit: _partial, eight pixels per byte, x % 8 selecting the bit LSB-first,
//        and a set bit meaning ink.
constexpr int kThreeBitStride = 1024 / 2;
constexpr int kOneBitStride = 1024 / 8;
constexpr size_t kOneBitBytes = 1024u * 758u / 8u;

}  // namespace

void DeviceDisplay::blit_3bit() {
  const uint8_t* src = fb_.pixels();
  uint8_t* dst = panel_->DMemory4Bit;
  if (dst == nullptr) return;
  const int w = fb_.width();
  const int h = fb_.height();
  for (int y = 0; y < h; ++y) {
    const uint8_t* s = src + static_cast<size_t>(y) * w;
    uint8_t* d = dst + static_cast<size_t>(y) * kThreeBitStride;
    for (int x = 0; x < w; x += 2) {
      *d++ = static_cast<uint8_t>(((s[x] >> 5) << 4) | (s[x + 1] >> 5));
    }
  }
}

void DeviceDisplay::blit_1bit() {
  // Atkinson-dither first, using the same code the simulator uses, so what
  // `--depth mono1` shows is what the panel shows. A hard threshold loses the
  // grey edges that antialiasing puts on small type, and small type on a
  // partial refresh is most of what a reader looks at.
  reduce_to_mono1(&fb_);

  const uint8_t* src = fb_.pixels();
  uint8_t* dst = panel_->_partial;
  if (dst == nullptr) return;
  const int w = fb_.width();
  const int h = fb_.height();
  for (int y = 0; y < h; ++y) {
    const uint8_t* s = src + static_cast<size_t>(y) * w;
    uint8_t* d = dst + static_cast<size_t>(y) * kOneBitStride;
    for (int xb = 0; xb < w / 8; ++xb) {
      const uint8_t* p = s + xb * 8;
      // Already reduced to exactly kInk or kPaper, so this test is exact.
      uint8_t bits = 0;
      if (p[0] < 128) bits |= 0x01;
      if (p[1] < 128) bits |= 0x02;
      if (p[2] < 128) bits |= 0x04;
      if (p[3] < 128) bits |= 0x08;
      if (p[4] < 128) bits |= 0x10;
      if (p[5] < 128) bits |= 0x20;
      if (p[6] < 128) bits |= 0x40;
      if (p[7] < 128) bits |= 0x80;
      *d++ = bits;
    }
  }
}

// partialUpdate() diffs the new frame against DMemoryNew, which the library
// treats as "what is currently on the panel" in 1-bit. Only the 1-bit paths
// ever sync it — a 3-bit display() leaves it holding a frame from pages ago.
// The next partial then decides that matching pixels need no driving and
// leaves the old ink where it was, which reads as the previous page bleeding
// through this one. So after a full refresh, tell the library the truth.
void DeviceDisplay::sync_reference_frame() {
  if (panel_->DMemoryNew == nullptr || panel_->_partial == nullptr) return;
  blit_1bit();
  std::memcpy(panel_->DMemoryNew, panel_->_partial, kOneBitBytes);
}

void DeviceDisplay::flush(RefreshMode mode) {
  uint32_t t0 = millis();
  // partialUpdate() is a no-op in 3-bit mode, which is why hal.h describes a
  // partial refresh as 1-bit: the mode is part of the contract, not a detail.
  const bool partial = mode == RefreshMode::Partial;
  panel_->selectDisplayMode(partial ? INKPLATE_1BIT : INKPLATE_3BIT);
  if (partial) {
    blit_1bit();
  } else {
    blit_3bit();
  }
  last_blit_ms_ = millis() - t0;

  t0 = millis();
  switch (mode) {
    case RefreshMode::Partial:
      panel_->partialUpdate(false, false);
      break;
    case RefreshMode::Full:
      panel_->display();
      sync_reference_frame();
      break;
    case RefreshMode::DeepClean:
      panel_->burnInClean(2, 20);
      panel_->display();
      sync_reference_frame();
      break;
  }
  last_flush_ms_ = millis() - t0;
}

void DeviceDisplay::set_frontlight(int level) {
  const int max = max_frontlight();
  frontlight_ = level < 0 ? 0 : (level > max ? max : level);
  // Frontlight is a member of the driver, not a base class.
  panel_->frontlight.setState(frontlight_ > 0);
  panel_->frontlight.setBrightness(static_cast<uint8_t>(frontlight_));
}

}  // namespace device
}  // namespace rsspaper
