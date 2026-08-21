# Reading model: a home page and a linear pass

Written 2026-08-21, after an overnight hardware test. The reader asked why the
paper opens onto a front page and section ledes to skim before you can read,
when what he wants is to wake to a summary, swipe once, and be in the first
story. This is the design for that change.

It is mostly subtraction. The oldest-first reading pass the reader wants —
wake, land, swipe right through every unread story in turn — already exists and
is unchanged by this work. What changes is everything in front of it.

## What exists today

An `Edition` is one flat `std::vector<Page>` split by `browse_page_count`
(`edition.h:103`): pages `[0, browse_page_count)` are the composed
**front-of-paper** — a newspaper front page (nameplate, lead headline, section
teasers) and then one lede page per section — and everything after is story
text. You land in `ReaderMode::Browse` on page 0, which is _drawn_ at read time
by `render_contents` (`reader.cpp:329`) rather than composed: a numbered list
of what is left. Swipe right there calls `begin_reading()` (`reader.cpp:441`),
which enters `ReaderMode::Article` — the pass — at the oldest unread story.

So there are two ways into a story today. The pass (`Article` mode:
`show_article_at`, `next_article`, oldest-first via `reading_order()`), which
marks each story read on arrival (`mark_current_read`, `reader.cpp:436`). And
lede-tapping (`open_story_at` → `ReaderMode::Story`, `reader.cpp:82`), a bounded
read that does _not_ mark anything read. The reader has five modes: `Browse`,
`Article`, `Story`, `Sections` (a jump-list overlay), `Finished`.

The map that established all of this is in the PR discussion; the file/line
references below are from it.

## The decision

Wake onto a **home page** — a summary, not an index. The **first swipe (right)
opens the oldest unread story**. From there the pass is as it is now: swipe
right for the next story, and — the one gesture change — swipe **down** to keep
reading further into the current story. The long-press home gesture still
returns to the home page.

Four choices were settled explicitly before this was written:

- **The composed newspaper front page is deleted.** The reader wants to read,
  not skim a front page. The DIARIUM nameplate is not lost with it — it moves
  onto the home page.
- **The home page is kept and rebuilt.** It is the "weird" screen the test
  complained about; it becomes the thing you wake to.
- **The section jump-list is dropped.** Reading is a line; there is nothing to
  jump around in.
- **The colophon is dropped.** The "why the paper is this length" note goes
  away rather than moving.

## The new model

### Modes: five become three

Keep `Home`, `Article`, `Finished`. Delete `Browse`, `Story`, `Sections`.

`Home` is what `Browse` page 0 was — the drawn summary — but it is now a mode of
its own with no composed pages behind it. `Article` and `Finished` are
unchanged. `go_home()` (`reader.cpp:515`) retargets from "Browse page 0" to
`Home`.

The entry point moves. Today bootstrap (`device/main.cpp:444`,
`sim/cmd_read.cpp:175`) lands in `Browse` at page 0, which happens to draw the
summary. After this change the initial mode is `Home` explicitly. Nothing else
in bootstrap changes: the summary is drawn, and the first right swipe runs the
`begin_reading()` that already exists.

### Gestures

Only `Article` mode's vertical axis changes, and only to match "swipe down to
keep reading":

| Gesture        | Today (`Article`)        | After                      |
| -------------- | ------------------------ | -------------------------- |
| Swipe right    | next unread story        | next unread story          |
| Swipe left     | previous story           | previous story             |
| Swipe **down** | back a page in the story | **further into the story** |
| Swipe up       | further into the story   | back a page in the story   |

`Home` mode: swipe right → `begin_reading()`. Other gestures are no-ops there
(swipe-down no longer toggles a section list, because there is no longer one).
`Finished` is unchanged.

### The edition: story text only

`compose_edition` (`edition.cpp:115`) loses two of its three passes. The
front-page pass (`front_tmpl`, the lead flow, `front_page_per_section`, and the
`stats.front_page_overflow` it produced) and the section-lede pass (`lede_tmpl`,
`SectionHead`/`LedeKicker`/`LedeHead`/`LedeText`, and the `LedeSpan`/`lede_page`/
`lede_bounds` bookkeeping) both go. The colophon flow appended during the lede
pass goes with them. What remains is the story-text pass (`story_tmpl`), which
already produces the only pages an edition will now hold.

`browse_page_count` collapses to 0 — there are no browse pages — and
`colophon_page` is removed. `StoryRef` (`edition.h:78`) keeps `key`,
`first_page`, `page_count`, `published`, and `section`; it drops `lede_page` and
`lede_bounds`, which only `open_story_at`/`story_at` read. `section_marks` and
`Edition::story_at` (`edition.h:125`) are deleted.

This is worth stating plainly because it is the largest single deletion in the
codebase's history: the front page is the product's showpiece, and it is going.
The reader has seen it (it was rendered in portrait during the orientation work)
and chose reading over skimming.

### The home page

`render_contents` becomes `render_home` (and `contents.{h,cpp}` becomes
`home.{h,cpp}`). It stops being a tappable index — nothing is tapped into any
more — and becomes a dashboard drawn from the edition and the read-state:

- The **DIARIUM masthead and the date line** at the top. This is the nameplate's
  new and only home.
- The **unread count**, and a **per-section breakdown** of it — the section is
  still on every `StoryRef`, so "Technology 6 · World 4 · Science 2" costs
  nothing to compute.
- The edition's **freshness** ("composed 05:30") and the **battery** mark.
- A "**swipe right to begin**" hint at the foot, where the contents page's hint
  already sits.

It is deliberately _not_ a list of headlines. A headline list is what the test
called weird, and in a linear model it promises a navigability the reader no
longer has. Counts and status are a genuinely different screen, and they answer
the test's actual complaint — "show how many unreads there are and some other
information."

### Read-state is untouched

The README's promise — the composer dedups against what was _read_, not what was
printed — lives in `compose.cpp:210` (cross-edition) and `mark_current_read`
(`reader.cpp:411`, in-edition). Both key off `StoryRef.key` and the `SeenStore`,
never off `lede_page`. Deleting the front-of-paper changes nothing here. A story
is still marked read the instant it becomes the current article, and what you
did not reach is still in tomorrow's paper.

## What gets deleted

For the plan's benefit, the full removal surface:

- `edition.cpp`: front-page pass (~~`200–283`), section-lede pass (~~`285–438`
  minus the `StoryRef.first_page`/`page_count` writes, which move to the story
  pass), colophon flow (~`350–400`).
- `edition.h`: `browse_page_count`, `colophon_page`, `section_marks`,
  `StoryRef::lede_page`, `StoryRef::lede_bounds`, `Edition::story_at`.
- `reader.cpp`/`.h`: `ReaderMode::Browse`/`Story`/`Sections`; `open_story_at`,
  the `Story` branches in `next_page`/`previous_page`/`back`, `return_page_`,
  `have_story_`; `toggle_sections`, `jump_to_section`, `render_section_overlay`.
- `contents.{h,cpp}`: renamed and rebuilt as `home.{h,cpp}`.

Unaffected: `reading_order()`, the `order_`/`order_pos_` walk, `show_article_at`,
`next_article`/`previous_article`/`scroll_*`, `mark_current_read`, `SeenStore`,
compose-time dedup, the story-text pass, and `Session`.

## Testing

The change is large in the test suite because so much of it exercises the
front-of-paper. `reader_test.cpp` loses its `Browse`/`Story`/`open_story_at`/
section-overlay cases and gains: initial mode is `Home`; a right swipe from
`Home` enters `Article` at the oldest unread story; swipe-down advances within a
story and swipe-up retreats; `go_home()` returns to `Home` from mid-pass.
`edition_test.cpp` loses `browse_page_count`/`story_at`/`lede_bounds` assertions;
an edition's pages are now all story text, and the count should equal the sum of
`StoryRef::page_count`. A `home` render test parallels the old contents test:
the masthead, an unread count, and the per-section line are present.

The device firmware must still build (`make device`), and the portability gate
must stay clean — `home.cpp` inherits `contents.cpp`'s place in `src/core/` and
must include no platform header.

## Sequencing

One coherent change, shipped together, but the plan can stage it so each step
builds and tests green:

1. Rebuild the home page (`render_home`) as a dashboard, still reached as it is
   today, so it can be seen on the panel before anything is deleted.
2. Re-anchor entry to `Home` and flip the `Article` vertical gestures.
3. Delete the front-of-paper: the composer passes, the reader modes, the dead
   `StoryRef`/`Edition` members, and their tests.

## Out of scope

Article images (#4) and the 30–45 s wake time (#2) are separate work. This
change assumes the portrait default (`feature/portrait-default`) it is branched
from: the home page is designed for the 758×1024 page.
