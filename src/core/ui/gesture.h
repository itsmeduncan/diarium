// Turning touch points into intentions.
//
// Portable, and above the HAL, so a device implementation never has to have an
// opinion about what a swipe is — and so this can be tested without hardware.
//
// The thresholds here are e-ink thresholds. On a phone, a swipe is confirmed
// while your finger is still moving and the screen has already responded. Here
// nothing visible happens for 225 ms after the gesture completes, so a reader
// gets no feedback to correct against mid-stroke. That means: commit on
// release rather than during the drag, and use a generous dead zone, because
// an accidental page turn costs a full refresh to undo.
#pragma once

#include <cstdint>

namespace rsspaper {

enum class Gesture : uint8_t {
  None,
  Tap,
  LongPress,
  SwipeLeft,   // finger moves left: go forward, like turning a page
  SwipeRight,  // go back
  SwipeUp,
  SwipeDown,
};

struct GestureEvent {
  Gesture kind = Gesture::None;
  // Where the gesture started. For a tap that is where the reader pointed.
  int x = 0;
  int y = 0;
};

struct GestureLimits {
  // Movement beyond this is a swipe, not a tap. Roughly 5 mm at 212 PPI.
  int swipe_threshold_px = 44;
  // Movement under this doesn't cancel a long press.
  int long_press_slop_px = 18;
  uint32_t long_press_ms = 650;
  // A stroke slower than this is a drag someone thought better of.
  uint32_t swipe_timeout_ms = 900;
  // Ignore a second touch arriving within this of the last one being lifted;
  // e-ink latency makes double-taps easy to produce by accident.
  uint32_t debounce_ms = 120;
};

class GestureRecognizer {
 public:
  explicit GestureRecognizer(GestureLimits limits = GestureLimits())
      : limits_(limits) {}

  // Feed the polled touch state each cycle. Returns a gesture at most once
  // per touch, on release — or for a long press, as soon as it qualifies.
  //
  // `x` and `y` are ignored when `touching` is false: a panel that has been
  // released reports no points, so the release coordinates are whatever the
  // caller happened to pass. The gesture is measured from the last position
  // seen while the finger was still down, which means a caller must poll
  // during the stroke and not only at its ends.
  GestureEvent update(bool touching, int x, int y, uint32_t now_ms);

  void reset();

 private:
  GestureLimits limits_;

  bool down_ = false;
  int start_x_ = 0;
  int start_y_ = 0;
  int last_x_ = 0;
  int last_y_ = 0;
  uint32_t start_ms_ = 0;
  uint32_t released_ms_ = 0;
  bool fired_ = false;  // a long press already reported for this touch
  int max_travel_ = 0;
};

const char* gesture_name(Gesture g);

}  // namespace rsspaper
