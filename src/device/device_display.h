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

  uint32_t last_blit_ms() const { return last_blit_ms_; }
  uint32_t last_flush_ms() const { return last_flush_ms_; }

 private:
  void blit_3bit();
  void blit_1bit();
  void sync_reference_frame();

  Inkplate* panel_;
  Framebuffer fb_;  // 776 KB; must be allocated before fonts and edition
  int frontlight_ = 0;
  uint32_t last_blit_ms_ = 0;
  uint32_t last_flush_ms_ = 0;
};

}  // namespace device
}  // namespace rsspaper
