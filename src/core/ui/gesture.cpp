#include "core/ui/gesture.h"

namespace diarium {
namespace {

int abs_i(int v) { return v < 0 ? -v : v; }

}  // namespace

const char* gesture_name(Gesture g) {
  switch (g) {
    case Gesture::None: return "none";
    case Gesture::Tap: return "tap";
    case Gesture::LongPress: return "long-press";
    case Gesture::SwipeLeft: return "swipe-left";
    case Gesture::SwipeRight: return "swipe-right";
    case Gesture::SwipeUp: return "swipe-up";
    case Gesture::SwipeDown: return "swipe-down";
  }
  return "none";
}

void GestureRecognizer::reset() {
  down_ = false;
  fired_ = false;
  max_travel_ = 0;
}

GestureEvent GestureRecognizer::update(bool touching, int x, int y,
                                       uint32_t now_ms) {
  GestureEvent out;

  if (touching && !down_) {
    // Ignore a touch that lands immediately after the last one lifted.
    if (released_ms_ != 0 && now_ms - released_ms_ < limits_.debounce_ms) {
      return out;
    }
    down_ = true;
    fired_ = false;
    max_travel_ = 0;
    start_x_ = last_x_ = x;
    start_y_ = last_y_ = y;
    start_ms_ = now_ms;
    return out;
  }

  if (touching && down_) {
    last_x_ = x;
    last_y_ = y;
    const int travel = abs_i(x - start_x_) + abs_i(y - start_y_);
    if (travel > max_travel_) max_travel_ = travel;

    // A long press fires while the finger is still down: waiting for release
    // would leave the reader holding a button with no idea it worked.
    if (!fired_ && max_travel_ <= limits_.long_press_slop_px &&
        now_ms - start_ms_ >= limits_.long_press_ms) {
      fired_ = true;
      out.kind = Gesture::LongPress;
      out.x = start_x_;
      out.y = start_y_;
    }
    return out;
  }

  if (!touching && down_) {
    down_ = false;
    released_ms_ = now_ms;
    if (fired_) return out;  // the long press was this touch's gesture

    const int dx = last_x_ - start_x_;
    const int dy = last_y_ - start_y_;
    const uint32_t elapsed = now_ms - start_ms_;

    out.x = start_x_;
    out.y = start_y_;

    if (elapsed > limits_.swipe_timeout_ms) {
      // Too slow to be a swipe, too far to be a tap: someone changed their
      // mind. Do nothing, which is the cheapest correct answer on e-ink.
      if (abs_i(dx) > limits_.swipe_threshold_px ||
          abs_i(dy) > limits_.swipe_threshold_px) {
        out.kind = Gesture::None;
        return out;
      }
    }

    if (abs_i(dx) > abs_i(dy)) {
      if (abs_i(dx) >= limits_.swipe_threshold_px) {
        out.kind = dx < 0 ? Gesture::SwipeLeft : Gesture::SwipeRight;
        return out;
      }
    } else {
      if (abs_i(dy) >= limits_.swipe_threshold_px) {
        out.kind = dy < 0 ? Gesture::SwipeUp : Gesture::SwipeDown;
        return out;
      }
    }

    out.kind = Gesture::Tap;
    return out;
  }

  return out;
}

}  // namespace diarium
