// The simulator's HAL: the same six interfaces the Inkplate will implement,
// backed by a PNG writer, the keyboard, and the local filesystem.
//
// Everything above these classes is the firmware. If a change to the reader
// needs something here that the device can't provide, that's the signal the
// change is wrong.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "hal/hal.h"
#include "sim/png_writer.h"
#include "sim/sim_storage.h"

namespace diarium {
namespace sim {

// Writes each flush to a numbered PNG and records what refresh mode was asked
// for, so the refresh policy can be inspected without hardware.
class SimDisplay final : public IDisplay {
 public:
  SimDisplay(std::string out_dir, Depth depth)
      : out_dir_(std::move(out_dir)), depth_(depth) {}

  int width() const override { return fb_.width(); }
  int height() const override { return fb_.height(); }
  Framebuffer& framebuffer() override { return fb_; }

  void flush(RefreshMode mode) override;

  void set_frontlight(int level) override {
    frontlight_ = level < 0 ? 0 : (level > 63 ? 63 : level);
  }
  int frontlight() const override { return frontlight_; }

  size_t flush_count() const { return flushes_.size(); }
  const std::vector<RefreshMode>& flushes() const { return flushes_; }
  const std::string& last_path() const { return last_path_; }
  // Milliseconds the panel would have spent on refreshes so far: partial
  // ~225 ms, full ~1.26 s on the 6FLICK. A cheap way to notice a policy that
  // would feel slow.
  int refresh_cost_ms() const;

 private:
  Framebuffer fb_;
  std::string out_dir_;
  Depth depth_;
  int frontlight_ = 0;
  std::vector<RefreshMode> flushes_;
  std::string last_path_;
};

// Touches are synthesised by the driver rather than read from a panel.
class SimInput final : public IInput {
 public:
  size_t poll(TouchPoint* out, size_t max) override {
    if (!touching_ || max == 0) return 0;
    out[0] = point_;
    return 1;
  }
  uint32_t millis() const override { return millis_; }

  void set_touch(bool down, int x, int y) {
    touching_ = down;
    point_.x = x;
    point_.y = y;
  }
  void advance(uint32_t ms) { millis_ += ms; }

 private:
  bool touching_ = false;
  TouchPoint point_;
  uint32_t millis_ = 1000;
};

// No wall clock in the core: the date is an input, so runs are reproducible.
class SimClock final : public IClock {
 public:
  explicit SimClock(Epoch now) : now_(now) {}
  Epoch now() const override { return now_; }
  void set_wake_alarm(Epoch when) override { alarm_ = when; }
  Epoch alarm() const { return alarm_; }

 private:
  Epoch now_;
  Epoch alarm_ = kNoDate;
};

class SimPower final : public IPower {
 public:
  void deep_sleep_until(Epoch when) override { slept_until_ = when; }
  int battery_millivolts() const override { return 3900; }
  bool on_external_power() const override { return true; }
  Epoch slept_until() const { return slept_until_; }

 private:
  Epoch slept_until_ = kNoDate;
};

// Serves the fixture corpus in place of the network. The real client (issue
// #3) implements the same interface, including the 304 path — which this can
// simulate, since a second request for an unchanged fixture should behave the
// way an unchanged feed does.
class SimHttpClient final : public IHttpClient {
 public:
  SimHttpClient(std::string fixtures_dir, bool honour_conditional)
      : fixtures_dir_(std::move(fixtures_dir)),
        honour_conditional_(honour_conditional) {}

  std::unique_ptr<ByteSource> get(const HttpRequest& request,
                                  HttpResponse* out) override;

 private:
  std::string fixtures_dir_;
  bool honour_conditional_;
};

}  // namespace sim
}  // namespace diarium
