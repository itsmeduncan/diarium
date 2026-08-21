// `diarium-sim read` — drives the real reader through the real HAL.
//
// The keyboard stands in for the panel: a keystroke becomes a synthesised
// touch, goes through the same gesture recogniser the device will use, and
// lands in the same `Reader`. Every flush writes a PNG, so you can watch the
// sequence a reader would actually see — including which refreshes were
// partial and which were full.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "core/base/datetime.h"
#include "core/base/str.h"
#include "core/config/feeds_config.h"
#include "core/edition/edition.h"
#include "core/edition/edition_store.h"
#include "core/io/file_byte_source.h"
#include "core/feed/feed_parser.h"
#include "core/text/font_pack.h"
#include "core/ui/reader.h"
#include "sim/commands.h"
#include "sim/fixtures.h"
#include "sim/sim_hal.h"

namespace diarium {
namespace sim {
namespace {

void print_help() {
  std::printf(
      "\n"
      "  n / space   the next unread article    (swipe right)\n"
      "  p           the previous article       (swipe left)\n"
      "  j / k       scroll down / up within an article  (swipe up/down)\n"
      "  1-9         open the Nth story on this page   (tap a lede)\n"
      "  b           back to where you were     (swipe up in the overlay)\n"
      "  g           home: the contents page   (long press, bottom left)\n"
      "  s           section list               (swipe down)\n"
      "  t X Y       tap an exact point, for checking hit regions\n"
      "  ?           this help\n"
      "  q           quit\n"
      "\n");
}

// Synthesises a press-move-release through the gesture recogniser, so the
// keyboard exercises the same path a finger will.
void synth_swipe(SimInput* input, Reader* reader, int dx, int dy) {
  const int cx = kPageWidth / 2;
  const int cy = kPageHeight / 2;
  GestureRecognizer local;
  input->advance(20);
  GestureEvent e = local.update(true, cx, cy, input->millis());
  input->advance(120);
  e = local.update(true, cx + dx / 2, cy + dy / 2, input->millis());
  input->advance(120);
  e = local.update(false, cx + dx, cy + dy, input->millis());
  if (e.kind != Gesture::None) reader->handle(e);
}

void synth_tap(SimInput* input, Reader* reader, int x, int y) {
  GestureRecognizer local;
  input->advance(200);
  local.update(true, x, y, input->millis());
  input->advance(60);
  const GestureEvent e = local.update(false, x, y, input->millis());
  if (e.kind != Gesture::None) reader->handle(e);
}

void synth_long_press(SimInput* input, Reader* reader, int x, int y) {
  GestureRecognizer local;
  input->advance(200);
  GestureEvent e = local.update(true, x, y, input->millis());
  input->advance(800);  // past long_press_ms, without moving
  e = local.update(true, x, y, input->millis());
  if (e.kind != Gesture::None) reader->handle(e);
  input->advance(20);
  local.update(false, x, y, input->millis());
}

// The ledes on the current page, top to bottom, so a digit key can pick one.
std::vector<const StoryRef*> ledes_on(const Edition& ed, size_t page) {
  std::vector<const StoryRef*> out;
  for (const StoryRef& s : ed.stories) {
    if (s.lede_page == page) out.push_back(&s);
  }
  for (size_t i = 0; i + 1 < out.size(); ++i) {
    for (size_t j = i + 1; j < out.size(); ++j) {
      if (out[j]->lede_bounds.y < out[i]->lede_bounds.y) {
        const StoryRef* t = out[i];
        out[i] = out[j];
        out[j] = t;
      }
    }
  }
  return out;
}

}  // namespace

int cmd_read(const std::vector<std::string>& args) {
  const std::string config_path = flag(args, "--config", "config/feeds.toml");
  const std::string fonts_path = flag(args, "--fonts", "build/literata.rfp");
  const std::string out_dir = flag(args, "--out", "out/read");
  const std::string fixtures_dir =
      flag(args, "--fixtures", "test/fixtures/feeds");

  FeedList config;
  std::string error;
  if (!load_feeds_toml(config_path, &config, &error)) {
    std::fprintf(stderr, "read: %s\n", error.c_str());
    return 1;
  }
  FontPack fonts;
  if (!fonts.load_file(fonts_path, &error)) {
    std::fprintf(stderr, "read: %s\n", error.c_str());
    return 1;
  }

  // Prefer a saved edition, which is what the device does: composition
  // happened at wake, and reading it again should cost nothing.
  Edition ed;
  const std::string load_path = flag(args, "--edition", "out/edition.rspe");
  std::string blob;
  bool loaded = false;
  if (!has_flag(args, "--recompose") && read_file(load_path, &blob)) {
    std::string load_error;
    if (deserialize_edition(blob, &ed, &load_error)) {
      loaded = true;
      std::printf("loaded %s (%zu KB, no re-parse, no re-layout)\n",
                  load_path.c_str(), blob.size() / 1024);
    } else {
      std::fprintf(stderr, "read: %s — composing instead\n",
                   load_error.c_str());
    }
  }
  if (!loaded) {
    FixtureComposeOptions fixture_opts;
    fixture_opts.fixtures_dir = fixtures_dir;
    fixture_opts.fresh = true;
    FixtureComposeReport report;
    ed = compose_from_fixtures(config, fonts, fixture_opts, &report);
  }
  if (ed.pages.empty()) {
    std::fprintf(stderr, "read: the edition is empty\n");
    return 1;
  }

  ensure_output_dir(out_dir);
  SimDisplay display(out_dir, Depth::Grey8);
  SimInput input;
  SimClock clock(ed.date);
  SimPower power;
  SimStorage storage(out_dir);
  SimHttpClient http(fixtures_dir, true);

  Hal hal;
  hal.display = &display;
  hal.input = &input;
  hal.clock = &clock;
  hal.power = &power;
  hal.storage = &storage;
  hal.http = &http;

  Reader reader(ed, fonts, hal);
  // The reading order is built here, so this is not optional: without it the
  // pass has nothing to walk and the first swipe says the news ran out.
  reader.load_read_state("read.dat");
  reader.render();

  std::printf("Diarium — %s\n", format_masthead_date(ed.date).c_str());
  std::printf("%zu pages to flip through, %zu stories behind them\n",
              ed.browse_page_count, ed.stories.size());
  print_help();

  char line[256];
  for (;;) {
    const std::vector<const StoryRef*> ledes =
        reader.mode() == ReaderMode::Browse ? ledes_on(ed, reader.current_page())
                                            : std::vector<const StoryRef*>();

    std::printf("[%s] %s%s\n> ", display.last_path().c_str(),
                reader.position().c_str(),
                ledes.empty() ? ""
                              : ("  (" + std::to_string(ledes.size()) +
                                 " stories here)").c_str());
    std::fflush(stdout);

    if (std::fgets(line, sizeof(line), stdin) == nullptr) break;
    std::string cmd = trim(line);
    if (cmd.empty()) cmd = "n";

    if (cmd == "q") break;
    if (cmd == "?") {
      print_help();
      continue;
    }

    bool changed = false;
    if (cmd == "n" || cmd == " ") {
      // Rightwards is onward through the news, which is what the reader does
      // now; the key labelled "next" has to send the gesture that means next.
      synth_swipe(&input, &reader, 200, 0);
      changed = true;
    } else if (cmd == "p") {
      synth_swipe(&input, &reader, -200, 0);
      changed = true;
    } else if (cmd == "j") {
      synth_swipe(&input, &reader, 0, -200);
      changed = true;
    } else if (cmd == "k") {
      synth_swipe(&input, &reader, 0, 200);
      changed = true;
    } else if (cmd == "s") {
      synth_swipe(&input, &reader, 0, 200);
      changed = true;
    } else if (cmd == "g") {
      synth_long_press(&input, &reader, 40, kPageHeight - 40);
      changed = true;
    } else if (cmd == "b") {
      changed = reader.back();
    } else if (cmd[0] == 't') {
      int x = 0, y = 0;
      if (std::sscanf(cmd.c_str(), "t %d %d", &x, &y) == 2) {
        synth_tap(&input, &reader, x, y);
        changed = true;
      } else {
        std::printf("  usage: t <x> <y>\n");
      }
    } else if (cmd[0] >= '1' && cmd[0] <= '9') {
      const size_t which = static_cast<size_t>(cmd[0] - '1');
      if (reader.mode() == ReaderMode::Sections) {
        changed = reader.jump_to_section(which);
      } else if (which < ledes.size()) {
        const Rect& r = ledes[which]->lede_bounds;
        std::printf("  opening \"%s\"\n", ledes[which]->title.c_str());
        synth_tap(&input, &reader, r.x + r.w / 2, r.y + r.h / 2);
        changed = true;
      } else {
        std::printf("  no story %zu on this page\n", which + 1);
      }
    } else {
      std::printf("  unknown key '%s' — press ? for help\n", cmd.c_str());
    }

    if (changed) reader.tick();
  }

  std::printf(
      "\n%zu refreshes (%d partial, %d full) — about %.1f s of panel time\n",
      display.flush_count(),
      static_cast<int>(std::count(display.flushes().begin(),
                                  display.flushes().end(),
                                  RefreshMode::Partial)),
      static_cast<int>(std::count(display.flushes().begin(),
                                  display.flushes().end(), RefreshMode::Full)),
      display.refresh_cost_ms() / 1000.0);
  std::printf("frames written to %s/\n", out_dir.c_str());
  return 0;
}

}  // namespace sim
}  // namespace diarium
