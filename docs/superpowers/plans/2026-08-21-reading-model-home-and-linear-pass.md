# Reading model: home page and linear pass — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the composed front-of-paper (front page + section ledes) with a wake-to-summary home page whose first right swipe drops straight into the oldest unread story.

**Architecture:** Mostly subtraction. The oldest-first `Article` pass and all read-state already exist and don't change. We rebuild the drawn contents page into a status dashboard (`render_home`), flip the `Article` vertical gestures, collapse the reader's five modes to three (`Home`/`Article`/`Finished`), then delete the composer's front-page and lede passes and the now-dead `Edition`/`StoryRef` members.

**Tech Stack:** Portable C++17 (`src/core/`), doctest, the Makefile/CMake dual build. Runs on the desktop sim and the Inkplate identically.

**Spec:** `docs/superpowers/specs/2026-08-21-reading-model-home-and-linear-pass-design.md`

## Global Constraints

- **`src/core/` includes no platform header.** `home.cpp` inherits `contents.cpp`'s place in the portable core. `tools/check-portability.sh` (in `make check`) gates this.
- **Nothing scales with input size.** No new unbounded buffers; the home page's per-section tally is bounded by the section count.
- **Run tests from the repository root** — fixtures are read by relative path.
- **Build both ways:** `make tests && make check` is the gate; the device must still build with `make device`.
- **The nameplate is `DIARIUM`** (masthead title), date line "Saturday, 15 August 2026" style via `format_masthead_date`.
- **Commit messages:** imperative subject; end with the `Co-Authored-By` / `Claude-Session` trailers used on this branch.

---

## File structure

- `src/core/ui/home.h`, `home.cpp` — **new**, replacing `contents.{h,cpp}`. Draws the wake dashboard: masthead, unread count, per-section breakdown, freshness strap, swipe hint. One responsibility: render the home screen.
- `src/core/ui/reader.h`, `reader.cpp` — modes collapse to three; `Home` renders via `home.h`; `Article` gestures flip; the lede/story/sections machinery is deleted.
- `src/core/edition/edition.h`, `edition.cpp` — `compose_edition` loses its front-page and lede passes; `Edition`/`StoryRef` lose the browse-only members.
- `test/home_test.cpp` — **new**, the dashboard's render test.
- `test/reader_test.cpp`, `test/edition_test.cpp` — updated for the new model.
- `src/sim/cmd_read.cpp` — keyboard driver stops calling the deleted reader methods.

---

## Task 1: The home dashboard (`render_home`)

Rebuild the drawn contents page as a status dashboard and render it in place of `render_contents`, still under the existing `Browse` page-0 path so it is visible on the panel before anything is deleted.

**Files:**

- Create: `src/core/ui/home.h`, `src/core/ui/home.cpp`
- Create: `test/home_test.cpp`
- Modify: `src/core/ui/reader.cpp` (the `render()` page-0 branch), `src/core/ui/reader.cpp` include line
- Delete at end of task: `src/core/ui/contents.h`, `src/core/ui/contents.cpp`

**Interfaces:**

- Produces: `void render_home(const FontPack& fonts, const Edition& edition, const std::vector<size_t>& order, const std::vector<bool>& unread, const std::string& strap, Framebuffer* fb);` — same parameter list as the old `render_contents`, so the reader's call site changes only in name.
- Consumes: `Edition::stories[i].section` (`std::string`), `MastheadInfo`, `PageRenderer::render_masthead` are available but `render_home` draws its own nameplate (as `contents.cpp` does today) to keep the file self-contained.

- [ ] **Step 1: Write the failing test**

Create `test/home_test.cpp`. It builds a tiny hand-made `Edition` (no font pack needed for the counting assertions; the render call is guarded on the pack like `layout_test.cpp`).

```cpp
#include <string>
#include <vector>

#include "core/edition/edition.h"
#include "core/render/framebuffer.h"
#include "core/text/font_pack.h"
#include "core/ui/home.h"
#include "doctest.h"

using namespace diarium;

namespace {
const FontPack* pack() {
  static FontPack fonts;
  static bool tried = false;
  if (!tried) {
    tried = true;
    std::string error;
    fonts.load_file("build/literata.rfp", &error);
  }
  return fonts.loaded() ? &fonts : nullptr;
}

Edition three_story_edition() {
  Edition ed;
  ed.title = "Diarium";
  ed.stories.push_back(StoryRef{});
  ed.stories.back().section = "Technology";
  ed.stories.push_back(StoryRef{});
  ed.stories.back().section = "Technology";
  ed.stories.push_back(StoryRef{});
  ed.stories.back().section = "World";
  return ed;
}
}  // namespace

TEST_CASE("home breakdown counts unread stories per section, in order") {
  const Edition ed = three_story_edition();
  const std::vector<size_t> order = {0, 1, 2};
  const std::vector<bool> unread = {true, false, true};  // one Tech read

  const HomeSummary s = summarize_home(ed, order, unread);
  CHECK(s.unread_total == 2);
  REQUIRE(s.sections.size() == 2);
  CHECK(s.sections[0].name == "Technology");
  CHECK(s.sections[0].count == 1);
  CHECK(s.sections[1].name == "World");
  CHECK(s.sections[1].count == 1);
}

TEST_CASE("home render fills the page and does not crash on a real pack") {
  const FontPack* fonts = pack();
  if (fonts == nullptr) return;  // no pack built; nothing to draw
  const Edition ed = three_story_edition();
  Framebuffer fb;
  render_home(*fonts, ed, {0, 1, 2}, {true, true, true}, "composed on device",
              &fb);
  // A drawn page is not blank paper: at least one inked pixel exists.
  bool any_ink = false;
  for (int y = 0; y < fb.height() && !any_ink; ++y) {
    for (int x = 0; x < fb.width(); ++x) {
      if (fb.pixels()[y * fb.width() + x] < 128) { any_ink = true; break; }
    }
  }
  CHECK(any_ink);
}
```

- [ ] **Step 2: Run it to verify it fails to compile**

Run: `make tests 2>&1 | tail -20`
Expected: FAIL — `core/ui/home.h` not found, `summarize_home`/`HomeSummary`/`render_home` undeclared.

- [ ] **Step 3: Write `home.h`**

Create `src/core/ui/home.h`:

```cpp
// The page you wake to: a summary of what is unread, not an index of it.
//
// Drawn rather than composed, because it is a view of what is left and that
// changes as you read while the edition does not. Reading is linear now — you
// swipe right from here into the oldest unread story — so this counts and
// orients rather than listing headlines to tap.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "core/edition/edition.h"
#include "core/render/framebuffer.h"
#include "core/text/font_pack.h"

namespace diarium {

// The unread breakdown, split out so it can be tested without a font pack.
struct HomeSummary {
  size_t unread_total = 0;
  struct Section {
    std::string name;
    size_t count = 0;
  };
  // Sections in the order they first appear in `order`, which is the paper's
  // running order.
  std::vector<Section> sections;
};

// `order` is story indices oldest-first; `unread` is the same length and says
// which are still to read.
HomeSummary summarize_home(const Edition& edition,
                           const std::vector<size_t>& order,
                           const std::vector<bool>& unread);

// Draws the home page: nameplate, the unread count and its per-section
// breakdown, the freshness strap, and the hint to swipe onward.
void render_home(const FontPack& fonts, const Edition& edition,
                 const std::vector<size_t>& order,
                 const std::vector<bool>& unread, const std::string& strap,
                 Framebuffer* fb);

}  // namespace diarium
```

- [ ] **Step 4: Write `home.cpp`**

Create `src/core/ui/home.cpp`. `summarize_home` is pure counting; `render_home` reuses the nameplate drawing from `contents.cpp` (copy the `upper()` helper and the nameplate block verbatim), then draws the dashboard instead of a list.

```cpp
#include "core/ui/home.h"

#include "core/base/datetime.h"
#include "core/layout/page.h"
#include "core/text/faces.h"

namespace diarium {
namespace {

std::string upper(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    out.push_back(c >= 'a' && c <= 'z' ? static_cast<char>(c - 32) : c);
  }
  return out;
}

}  // namespace

HomeSummary summarize_home(const Edition& edition,
                           const std::vector<size_t>& order,
                           const std::vector<bool>& unread) {
  HomeSummary out;
  for (size_t i = 0; i < order.size(); ++i) {
    if (i >= unread.size() || !unread[i]) continue;
    ++out.unread_total;
    const std::string& section = edition.stories[order[i]].section;
    bool found = false;
    for (HomeSummary::Section& s : out.sections) {
      if (s.name == section) { ++s.count; found = true; break; }
    }
    if (!found) out.sections.push_back({section, 1});
  }
  return out;
}

void render_home(const FontPack& fonts, const Edition& edition,
                 const std::vector<size_t>& order,
                 const std::vector<bool>& unread, const std::string& strap,
                 Framebuffer* fb) {
  fb->fill(kPaper);

  const int left = kSideMargin;
  const int right = page_width() - kSideMargin;
  const int width = right - left;

  const Face& lead = fonts.face(FaceId::Lead);
  const Face& head = fonts.face(FaceId::BodyBold);
  const Face& body = fonts.face(FaceId::Body);
  const Face& meta = fonts.face(FaceId::Meta);

  // The nameplate, in the same language as the sleep screen: letterspaced
  // caps, heavy rule, hairline under. This is the masthead's only home now.
  int y = 96;
  if (lead.valid()) {
    const std::string caps = upper(edition.title);
    const int tracking = 10 * kSubpixel;
    const int measured =
        lead.measure(caps) + tracking * static_cast<int>(caps.size());
    const int x = (page_width() * kSubpixel - measured) / 2;
    fb->draw_text_tracked(lead, caps, x, y, kInk, tracking);
  }
  y += 26;
  fb->fill_rect(left, y, width, 5, kInk);
  y += 12;
  if (meta.valid()) {
    const std::string date = format_masthead_date(edition.date);
    const int x = (page_width() * kSubpixel - meta.measure(date)) / 2;
    fb->draw_text(meta, date, x, y, kInk);
  }
  y += 10;
  fb->fill_rect(left, y, width, 1, 190);

  const HomeSummary summary = summarize_home(edition, order, unread);

  // The count, large, because it is the one number the page exists to show.
  y += 150;
  if (lead.valid()) {
    const std::string n = std::to_string(summary.unread_total);
    const std::string label = summary.unread_total == 1 ? " unread" : " unread";
    const int x = left * kSubpixel;
    const int after = fb->draw_text(lead, n, x, y, kInk);
    if (head.valid()) fb->draw_text(head, label, after, y, kInk);
  }

  // The breakdown, one section per line.
  y += 60;
  for (const HomeSummary::Section& s : summary.sections) {
    if (y > page_height() - 120) break;  // bounded by the page, like everything
    if (head.valid()) {
      const int after =
          fb->draw_text(head, s.name, left * kSubpixel, y, kInk);
      if (body.valid()) {
        fb->draw_text(body, "  " + std::to_string(s.count), after, y, kInk);
      }
    }
    y += 44;
  }

  // The strap and the hint, at the foot where the contents page kept them.
  if (meta.valid() && !strap.empty()) {
    const int x = (page_width() * kSubpixel - meta.measure(strap)) / 2;
    fb->fill_rect(left, page_height() - 74, width, 1, 200);
    fb->draw_text(meta, strap, x, page_height() - 52, kInk);
  }
  if (meta.valid()) {
    const std::string hint = upper("swipe right to begin");
    const int tracking = 4 * kSubpixel;
    const int measured =
        meta.measure(hint) + tracking * static_cast<int>(hint.size());
    const int x = (page_width() * kSubpixel - measured) / 2;
    fb->draw_text_tracked(meta, hint, x, page_height() - 34, 110, tracking);
  }
}

}  // namespace diarium
```

Note: confirm `Framebuffer::draw_text` returns the pen x (it is used that way in `contents.cpp`/`reader.cpp`). If a signature differs, match the existing call sites in `contents.cpp` exactly.

- [ ] **Step 5: Point the reader at `render_home`**

In `src/core/ui/reader.cpp`: change the include `#include "core/ui/contents.h"` to `#include "core/ui/home.h"`, and in `render()` replace the `render_contents(...)` call (the `Browse && page_ == 0` branch) with `render_home(fonts_, edition_, order_, unread, "composed on device", &hal_.display->framebuffer());` — same arguments, new name.

- [ ] **Step 6: Delete the old contents files and run tests**

```bash
git rm src/core/ui/contents.h src/core/ui/contents.cpp
```

Run: `make tests && ./bin/diarium-tests --test-case="*home*" && make check`
Expected: PASS (231 prior cases still green; two new `home` cases pass; portability clean). The glob build picks up `home.cpp` with no build-file edit.

- [ ] **Step 7: Eyeball it**

Run: `make sim && ./bin/diarium-sim compose --config config/feeds.toml --fonts build/literata.rfp --out out --fresh && ./bin/diarium-sim screens --edition out/edition.rspe --out out/screens`
Open `out/screens/contents.png` (still that filename in `cmd_screens`) and confirm the dashboard renders in portrait: nameplate, "N unread", per-section lines, hint.

- [ ] **Step 8: Commit**

```bash
git add src/core/ui/home.h src/core/ui/home.cpp test/home_test.cpp src/core/ui/reader.cpp
git commit -m "Rebuild the home page as an unread summary, not an index"
```

---

## Task 2: Flip the Article vertical gestures

Swipe down keeps reading (further into the story); swipe up steps back a page. Today it is the reverse.

**Files:**

- Modify: `src/core/ui/reader.cpp` (`handle()`, the `Article` switch, ~lines 202–209)
- Modify: `test/reader_test.cpp` (add/adjust the gesture-direction case)

**Interfaces:**

- Consumes: `Reader::scroll_down()` (further into the article, `article_page_ + 1`), `Reader::scroll_up()` (back a page). No signature changes.

- [ ] **Step 1: Write the failing test**

In `test/reader_test.cpp`, add (or adjust the existing scroll case) so it asserts the new mapping. Use the file's existing reader/edition fixture (a multi-page story in `Article` mode). Illustrative shape — match the file's fixture helpers:

```cpp
TEST_CASE("in an article, swipe down keeps reading and swipe up goes back") {
  const FontPack* fonts = pack();
  if (fonts == nullptr) return;
  Reader reader = reading_a_multipage_story(*fonts);  // enters Article, page 0
  const size_t p0 = reader.current_page();

  REQUIRE(reader.handle(swipe(Gesture::SwipeDown)));  // keep reading
  CHECK(reader.current_page() == p0 + 1);

  REQUIRE(reader.handle(swipe(Gesture::SwipeUp)));     // back a page
  CHECK(reader.current_page() == p0);
}
```

If the file already has a scroll test asserting the old (up = further) mapping, update that test rather than adding a duplicate.

- [ ] **Step 2: Run it to verify it fails**

Run: `./bin/diarium-tests --test-case="*swipe down keeps reading*"` (after `make tests`)
Expected: FAIL — swipe down currently calls `scroll_up`, so the page does not advance.

- [ ] **Step 3: Swap the two cases**

In `src/core/ui/reader.cpp`, `handle()`, the `Article` block:

```cpp
  if (mode_ == ReaderMode::Article) {
    switch (event.kind) {
      case Gesture::SwipeRight:
        return next_article();
      case Gesture::SwipeLeft:
        return previous_article();
      case Gesture::SwipeDown:
        return scroll_down();  // down keeps reading, further into the story
      case Gesture::SwipeUp:
        return scroll_up();    // up steps back a page
      default:
        return false;
    }
  }
```

Update the comment above the block ("up and down move within the article") to say "down keeps reading, up steps back".

- [ ] **Step 4: Run tests**

Run: `make tests && ./bin/diarium-tests --test-case="*swipe down keeps reading*" && make check`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/core/ui/reader.cpp test/reader_test.cpp
git commit -m "Swipe down to keep reading; up steps back a page"
```

---

## Task 3: Collapse the reader to Home / Article / Finished

Make `Home` the entry, delete the `Browse`/`Story`/`Sections` machinery. The composer still emits ledes at this point; the reader simply stops navigating to them, which keeps everything compiling. `Edition`'s browse members stay until Task 4.

**Files:**

- Modify: `src/core/ui/reader.h` (the `ReaderMode` enum, method declarations, members)
- Modify: `src/core/ui/reader.cpp` (`handle()`, `render()`, `go_home()`; delete `open_story_at`, `back`, `toggle_sections`, `jump_to_section`, `render_section_overlay`, `section_row_at`, `next_page`, `previous_page`, `open_story`)
- Modify: `src/sim/cmd_read.cpp` (stop calling deleted methods)
- Modify: `test/reader_test.cpp` (delete Browse/Story/Sections cases; add Home-entry cases)

**Interfaces:**

- Produces: `ReaderMode` is now `{ Home, Article, Finished }`. `Reader::mode()` returns it. Entry state is `Home`.
- Consumes: `render_home` (Task 1), `begin_reading()`, `next_article()`, `previous_article()`, `scroll_down()`, `scroll_up()`, `go_home()`, `mark_everything_read()` — all already exist.

- [ ] **Step 1: Write the failing tests**

In `test/reader_test.cpp`, add the entry-model cases (match the file's fixtures):

```cpp
TEST_CASE("the reader wakes on the home page") {
  const FontPack* fonts = pack();
  if (fonts == nullptr) return;
  Reader reader = fresh_reader(*fonts);  // load_read_state + nothing else
  CHECK(reader.mode() == ReaderMode::Home);
}

TEST_CASE("a right swipe from home opens the oldest unread story") {
  const FontPack* fonts = pack();
  if (fonts == nullptr) return;
  Reader reader = fresh_reader(*fonts);
  REQUIRE(reader.handle(swipe(Gesture::SwipeRight)));
  CHECK(reader.mode() == ReaderMode::Article);
}

TEST_CASE("the home gesture returns to the home page mid-pass") {
  const FontPack* fonts = pack();
  if (fonts == nullptr) return;
  Reader reader = fresh_reader(*fonts);
  reader.handle(swipe(Gesture::SwipeRight));            // into Article
  REQUIRE(reader.handle(long_press_home_corner()));     // long-press bottom-left
  CHECK(reader.mode() == ReaderMode::Home);
}
```

Delete the existing cases that exercise `Browse` page-flipping, `open_story_at`, `Story` mode, and the `Sections` overlay (search the file for `Story`, `open_story_at`, `Sections`, `jump_to_section`, `toggle_sections`).

- [ ] **Step 2: Run to verify it fails**

Run: `make tests 2>&1 | tail -20`
Expected: FAIL — `ReaderMode::Home` undeclared (and deleted-case symbols gone).

- [ ] **Step 3: Rewrite `reader.h`**

- Replace the enum:

```cpp
enum class ReaderMode : uint8_t {
  Home,       // the summary you wake to
  Article,    // one article in a continuous oldest-first pass
  Finished,   // the news ran out
};
```

- Delete the declarations: `const StoryRef* open_story() const;`, `bool next_page();`, `bool previous_page();`, `bool open_story_at(int x, int y);`, `bool back();`, `bool toggle_sections();`, `bool jump_to_section(size_t index);`, `void render_section_overlay();`, `size_t section_row_at(int y) const;`.
- Delete the members: `size_t return_page_`, `size_t story_index_` **only if** unused after edits (it is set by `show_article_at`; keep it if so — check the `.cpp`), `bool have_story_`, `bool confirm_mark_all_` (was Sections-only — delete), and `size_t page_` **only if** unused; `Article` renders from the story's `first_page + article_page_`, so `page_` may become dead — verify and delete if so, otherwise keep.
- Change the default: `ReaderMode mode_ = ReaderMode::Home;`.
- Update the `go_home()` doc comment from "contents page" to "home page".

Verify which of `page_`/`story_index_` survive by grepping the `.cpp` for their use in `render()`/`show_article_at` before deleting.

- [ ] **Step 4: Rewrite `handle()` and `render()` in `reader.cpp`**

`handle()` — after the frontlight and long-press-home guards, becomes:

```cpp
  if (mode_ == ReaderMode::Article) {
    switch (event.kind) {
      case Gesture::SwipeRight: return next_article();
      case Gesture::SwipeLeft:  return previous_article();
      case Gesture::SwipeDown:  return scroll_down();
      case Gesture::SwipeUp:    return scroll_up();
      default:                  return false;
    }
  }

  if (mode_ == ReaderMode::Finished) {
    if (event.kind == Gesture::SwipeLeft) return previous_article();
    return false;
  }

  // Home: rightwards is into the news. Nothing else does anything here.
  if (event.kind == Gesture::SwipeRight) return begin_reading();
  return false;
```

`render()` — the dispatch becomes:

```cpp
  if (mode_ == ReaderMode::Finished) {
    render_finished();
  } else if (mode_ == ReaderMode::Home) {
    std::vector<bool> unread;
    unread.reserve(order_.size());
    for (size_t i = 0; i < order_.size(); ++i) {
      unread.push_back(!read_.has(edition_.stories[order_[i]].key));
    }
    render_home(fonts_, edition_, order_, unread, "composed on device",
                &hal_.display->framebuffer());
  } else {
    if (page_ >= edition_.pages.size()) return;
    renderer_.render(edition_.pages[page_], &hal_.display->framebuffer());
  }
```

Remove the `is_front_page` masthead sub-branch (front pages are gone after Task 4; harmless to drop now since `Home` no longer routes through it).

Delete the function bodies: `open_story_at`, `back`, `toggle_sections`, `jump_to_section`, `render_section_overlay`, `section_row_at`, `next_page`, `previous_page`, `open_story`. Point `go_home()` at `Home`:

```cpp
bool Reader::go_home() {
  mode_ = ReaderMode::Home;
  pending_context_change_ = true;
  needs_render_ = true;
  return true;
}
```

- [ ] **Step 5: Fix the simulator driver**

In `src/sim/cmd_read.cpp`, remove key handlers that call the deleted methods (`open_story_at`, `next_page`, `previous_page`, `back`, `toggle_sections`, `jump_to_section`, `s`/`b`/`1-9` keys per README). Keep `n`/`p` (next/previous article), `j`/`k` (scroll), and the home long-press synthesis. Update the in-file help text to match.

- [ ] **Step 6: Run tests and the device build**

Run: `make tests && ./bin/diarium-tests && make check`
Expected: PASS, all cases.
Run: `make device 2>&1 | tail -3`
Expected: `[SUCCESS]` — the firmware still builds; `main.cpp` does not call the deleted methods (it drives via `reader.tick()`), but confirm.

- [ ] **Step 7: Commit**

```bash
git add src/core/ui/reader.h src/core/ui/reader.cpp src/sim/cmd_read.cpp test/reader_test.cpp
git commit -m "Collapse the reader to home, article, finished"
```

---

## Task 4: Delete the front-of-paper from the composer

Now that nothing reads them, remove the composed front page, the section ledes, the colophon, and the browse-only `Edition`/`StoryRef` members.

**Files:**

- Modify: `src/core/edition/edition.h` (`StoryRef`, `Edition`)
- Modify: `src/core/edition/edition.cpp` (`compose_edition`)
- Modify: `test/edition_test.cpp`

**Interfaces:**

- Produces: an `Edition` whose `pages` are all story text; `stories[i]` carries `key`, `title`, `section`, `source`, `first_page`, `page_count`, `truncated`, `published`. No `browse_page_count`, `colophon_page`, `section_marks`, `story_at`, `lede_page`, `lede_bounds`.

- [ ] **Step 1: Update the tests first**

In `test/edition_test.cpp`: delete assertions on `browse_page_count`, `colophon_page`, `story_at`, `lede_bounds`, `lede_page`, `section_marks`. Add/keep the invariant that the edition is all story text:

```cpp
TEST_CASE("an edition is nothing but story text now") {
  const Edition ed = compose_fixture_edition();  // the file's existing helper
  REQUIRE(!ed.stories.empty());
  size_t sum = 0;
  for (const StoryRef& s : ed.stories) {
    CHECK(s.first_page < ed.pages.size());
    CHECK(s.page_count >= 1);
    sum += s.page_count;
  }
  CHECK(sum == ed.pages.size());  // every page belongs to exactly one story
}
```

- [ ] **Step 2: Run to verify it fails to compile**

Run: `make tests 2>&1 | tail -20`
Expected: FAIL — deleted-member references in the still-present composer, or the new assertion failing because front/lede pages inflate `ed.pages`.

- [ ] **Step 3: Trim `StoryRef` and `Edition` in `edition.h`**

`StoryRef` becomes:

```cpp
struct StoryRef {
  uint64_t key = 0;
  std::string title;
  std::string section;
  std::string source;
  size_t first_page = 0;   // where the full text starts
  size_t page_count = 0;   // how long it runs
  bool truncated = false;  // the publisher's feed stops early
  Epoch published = kNoDate;
};
```

`Edition` loses `browse_page_count`, `colophon_page`, the `SectionMark` struct, `section_marks`, and `story_at`. Keep `date`, `title`, `pages`, `stats`, `stories`, `page_count()`, `reading_order()`. Update the struct's header comment: an edition is story text, walked oldest-first; there is no front-of-paper.

- [ ] **Step 4: Cut the two passes from `compose_edition`**

In `src/core/edition/edition.cpp`:

- Delete the **front-page block** — the `lead`/kicker/deck/byline flow, the per-section teaser loop, `front_tmpl`, its pagination, and the `stats.front_page_overflow` handling (the map placed this at ~`edition.cpp:200–283`).
- Delete the **section-lede block** — `lede_tmpl`, the `SectionHead`/`LedeKicker`/`LedeHead`/`LedeText` element emission, the `LedeSpan` tracking, and the `browse_page_count`/`section_marks`/`lede_page`/`lede_bounds` assignments (~`edition.cpp:285–438`).
- Delete the **colophon** flow appended in that block.
- Keep the **story-text pass** (`story_tmpl`, ~`441–476`). Ensure each `StoryRef` is populated here: `key`, `title`, `section`, `source`, `published`, `truncated` from the item, and `first_page`/`page_count` from the pagination. If those fields were previously set in the deleted lede pass, move their assignment into the story pass.
- In the **folio** step (~`479–505`), delete the `i < browse_page_count` branch; every page now takes the story folio.

Because the composer is large, work in the file with the anchors above (function names and the members being deleted), compiling after each deletion.

- [ ] **Step 5: Run the full gate**

Run: `make tests && ./bin/diarium-tests && make check`
Expected: PASS. Then `make device 2>&1 | tail -3` → `[SUCCESS]`.

- [ ] **Step 6: Eyeball a composed edition**

Run: `./bin/diarium-sim compose --config config/feeds.toml --fonts build/literata.rfp --out out --fresh`
Expected: the composer reports pages, and the first `out/page-001.png` is now **story text**, not the nameplate front page. Then `./bin/diarium-sim screens --edition out/edition.rspe --out out/screens` and confirm `out/screens/contents.png` (the home dashboard) still renders.

- [ ] **Step 7: Commit**

```bash
git add src/core/edition/edition.h src/core/edition/edition.cpp test/edition_test.cpp
git commit -m "Delete the front page and section ledes; an edition is story text"
```

---

## Self-review

**Spec coverage:**

- Wake to a home summary → Task 1 (`render_home`) + Task 3 (entry = `Home`). ✓
- First right swipe opens oldest unread story → Task 3 (`Home` → `begin_reading`). ✓
- Swipe down keeps reading → Task 2. ✓
- Delete composed front page; nameplate moves to home → Task 1 (masthead on home) + Task 4 (front-page pass deleted). ✓
- Home is counts + status, not a headline list → Task 1 (`summarize_home` + dashboard render). ✓
- Drop section jump-list → Task 3 (`Sections` mode, `toggle_sections`, `jump_to_section`, `render_section_overlay` deleted) + Task 4 (`section_marks` deleted). ✓
- Drop colophon → Task 4 (colophon flow + `colophon_page` deleted). ✓
- Modes five → three → Task 3. ✓
- Read-state/dedup unchanged → no task touches `SeenStore`, `mark_current_read`, or `compose.cpp:210`. ✓
- Portability + device build gates → every task's final step. ✓

**Placeholder scan:** No TBD/TODO. The two large-file deletions (Task 4 composer, Task 3 reader bodies) are specified by function name, member name, and the map's line anchors, with "compile after each deletion" — the concrete how for removing code from a big file, not a placeholder.

**Type consistency:** `render_home`/`summarize_home`/`HomeSummary` names and signatures match between `home.h` (Task 1) and the reader call site (Tasks 1, 3). `ReaderMode::{Home,Article,Finished}` is consistent across Tasks 2–3. `StoryRef` fields used in tests (Task 1: `section`; Task 4: `first_page`/`page_count`) match the trimmed struct.

**Note for the executor:** confirm `Framebuffer::draw_text`'s return value and the `FaceId` palette (`Lead`, `BodyBold`, `Body`, `Meta`) against `contents.cpp` before writing `home.cpp` — they are copied from that file's live usage but should be verified, not trusted.
