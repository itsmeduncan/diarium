// The reductions the panel actually performs. These live in core because the
// device and the simulator must agree — a mono1 PNG is only worth looking at
// if it is what the panel will show.
#include "core/render/reduce.h"

#include <set>

#include "core/render/framebuffer.h"
#include "doctest.h"

using namespace diarium;

namespace {

size_t ink_count(const Framebuffer& fb) {
  size_t n = 0;
  for (int y = 0; y < fb.height(); ++y) {
    for (int x = 0; x < fb.width(); ++x) {
      if (fb.get(x, y) == 0) ++n;
    }
  }
  return n;
}

}  // namespace

TEST_CASE("grey3 leaves at most eight levels") {
  Framebuffer fb(64, 64);
  for (int y = 0; y < 64; ++y) {
    for (int x = 0; x < 64; ++x) fb.set(x, y, static_cast<uint8_t>(x * 4));
  }
  reduce_to_grey3(&fb);

  std::set<uint8_t> levels;
  for (int y = 0; y < 64; ++y) {
    for (int x = 0; x < 64; ++x) levels.insert(fb.get(x, y));
  }
  CHECK(levels.size() <= 8);
}

TEST_CASE("grey3 keeps the extremes exact") {
  Framebuffer fb(8, 8);
  fb.fill(kPaper);
  fb.set(0, 0, kInk);
  reduce_to_grey3(&fb);
  CHECK(fb.get(0, 0) == kInk);
  CHECK(fb.get(7, 7) == kPaper);
}

TEST_CASE("mono1 leaves only ink and paper") {
  Framebuffer fb(96, 96);
  for (int y = 0; y < 96; ++y) {
    for (int x = 0; x < 96; ++x) fb.set(x, y, static_cast<uint8_t>((x * 8) % 256));
  }
  reduce_to_mono1(&fb);
  for (int y = 0; y < 96; ++y) {
    for (int x = 0; x < 96; ++x) {
      const uint8_t v = fb.get(x, y);
      CHECK((v == kInk || v == kPaper));
    }
  }
}

TEST_CASE("mono1 dithers a mid grey rather than flattening it") {
  // A hard threshold would turn 50% grey entirely to one colour. Dithering is
  // the whole reason small type stays legible on a partial refresh.
  Framebuffer fb(64, 64);
  fb.fill(127);
  reduce_to_mono1(&fb);

  const size_t ink = ink_count(fb);
  const size_t total = 64 * 64;
  CHECK(ink > total / 4);
  CHECK(ink < total * 3 / 4);
}

TEST_CASE("mono1 is deterministic") {
  Framebuffer a(48, 48);
  Framebuffer b(48, 48);
  for (int y = 0; y < 48; ++y) {
    for (int x = 0; x < 48; ++x) {
      const uint8_t v = static_cast<uint8_t>((x * 5 + y * 3) % 256);
      a.set(x, y, v);
      b.set(x, y, v);
    }
  }
  reduce_to_mono1(&a);
  reduce_to_mono1(&b);
  for (int y = 0; y < 48; ++y) {
    for (int x = 0; x < 48; ++x) CHECK(a.get(x, y) == b.get(x, y));
  }
}

TEST_CASE("mono1 leaves solid areas solid") {
  Framebuffer fb(32, 32);
  fb.fill(kPaper);
  reduce_to_mono1(&fb);
  CHECK(ink_count(fb) == 0);

  fb.fill(kInk);
  reduce_to_mono1(&fb);
  CHECK(ink_count(fb) == 32 * 32);
}
