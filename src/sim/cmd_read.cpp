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
#include <memory>
#include <string>
#include <vector>

#include "core/base/datetime.h"
#include "core/base/str.h"
#include "core/config/feeds_config.h"
#include "core/edition/edition.h"
#include "core/edition/edition_store.h"
#include "core/edition/edition_stream.h"
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

// Adapts a path on an IStorage into the core's RangedSource seam, so a
// StreamingEditionReader can open lazily against whatever the sim's storage
// holds without core/ ever knowing storage exists. `storage` and the file
// at `path` must outlive whatever this is handed to.
class StorageRangedSource final : public RangedSource {
 public:
  StorageRangedSource(IStorage* storage, std::string path)
      : storage_(storage), path_(std::move(path)) {}

  size_t size() const override { return storage_->size(path_); }
  bool read(size_t offset, size_t length, std::string* out) const override {
    return storage_->read_range(path_, offset, length, out);
  }

 private:
  IStorage* storage_;
  std::string path_;
};

void print_help() {
  std::printf(
      "\n"
      "  n / space   the next unread article    (swipe right)\n"
      "  p           the previous article       (swipe left)\n"
      "  j / k       scroll down / up within an article  (swipe up/down)\n"
      "  g           home: the summary page     (long press, bottom left)\n"
      "  t X Y       tap an exact point, for checking hit regions\n"
      "  ?           this help\n"
      "  q           quit\n"
      "\n");
}

// Synthesises a press-move-release through the gesture recogniser, so the
// keyboard exercises the same path a finger will.
void synth_swipe(SimInput* input, Reader* reader, int dx, int dy) {
  const int cx = page_width() / 2;
  const int cy = page_height() / 2;
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

  // Must match the orientation the edition was composed under, or the reader's
  // furniture lands at landscape coordinates over portrait pages.
  set_orientation(config.edition.orientation);

  FontPack fonts;
  if (!fonts.load_file(fonts_path, &error)) {
    std::fprintf(stderr, "read: %s\n", error.c_str());
    return 1;
  }

  // Prefer a saved edition, which is what the device does: composition
  // happened at wake, and reading it again should cost nothing. `compose`
  // now streams v5 by default, so that is tried first, lazily — only its
  // footer, header and index ever come resident here, the same as on
  // device. A v4 file (an older --save, or one written before this branch)
  // still loads whole, and if neither reads, the fixtures are composed
  // fresh.
  const std::string load_path = flag(args, "--edition", "out/edition.rspe");
  const bool allow_load = !has_flag(args, "--recompose");

  // load_path is a host path: root SimStorage at its directory and address
  // the basename, so an absolute --edition (e.g. CI's $RUNNER_TEMP) resolves
  // instead of being mangled into a bogus cwd-relative path under "./".
  const std::string::size_type load_slash = load_path.find_last_of('/');
  const std::string load_dir =
      load_slash == std::string::npos ? "." : load_path.substr(0, load_slash);
  const std::string load_name = load_slash == std::string::npos
                                    ? load_path
                                    : load_path.substr(load_slash + 1);
  SimStorage edition_storage(load_dir);
  StorageRangedSource edition_ranged(&edition_storage, load_name);
  StreamingEditionReader stream;
  bool loaded_v5 = false;
  if (allow_load) {
    std::string open_error;
    if (stream.open(edition_ranged, &open_error)) {
      loaded_v5 = true;
      std::printf(
          "loaded %s (v5, streamed — one story resident at a time)\n",
          load_path.c_str());
    }
  }

  Edition ed;  // only populated when the v5 path above did not pan out
  if (!loaded_v5) {
    bool loaded_v4 = false;
    std::string blob;
    if (allow_load && read_file(load_path, &blob)) {
      std::string load_error;
      if (deserialize_edition(blob, &ed, &load_error)) {
        loaded_v4 = true;
        std::printf("loaded %s (%zu KB, no re-parse, no re-layout)\n",
                    load_path.c_str(), blob.size() / 1024);
      } else {
        std::fprintf(stderr, "read: %s — composing instead\n",
                     load_error.c_str());
      }
    }
    if (!loaded_v4) {
      FixtureComposeOptions fixture_opts;
      fixture_opts.fixtures_dir = fixtures_dir;
      fixture_opts.fresh = true;
      FixtureComposeReport report;
      ed = compose_from_fixtures(config, fonts, fixture_opts, &report);
    }
  }

  const Epoch edition_date = loaded_v5 ? stream.date() : ed.date;
  const size_t story_count = loaded_v5 ? stream.index().size() : ed.stories.size();
  if (story_count == 0) {
    std::fprintf(stderr, "read: the edition is empty\n");
    return 1;
  }

  ensure_output_dir(out_dir);
  SimDisplay display(out_dir, Depth::Grey8);
  SimInput input;
  SimClock clock(edition_date);
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

  // Either constructor gives the same interface below; only how a page's
  // bytes get here differs.
  const std::unique_ptr<Reader> reader_storage =
      loaded_v5 ? std::unique_ptr<Reader>(new Reader(stream, fonts, hal))
               : std::unique_ptr<Reader>(new Reader(ed, fonts, hal));
  Reader& reader = *reader_storage;
  // The reading order is built here, so this is not optional: without it the
  // pass has nothing to walk and the first swipe says the news ran out.
  reader.load_read_state("read.dat");
  reader.render();

  std::printf("Diarium — %s\n", format_masthead_date(edition_date).c_str());
  std::printf("%zu stories in this edition\n", story_count);
  print_help();

  char line[256];
  for (;;) {
    std::printf("[%s] %s\n> ", display.last_path().c_str(),
                reader.position().c_str());
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
    } else if (cmd == "g") {
      synth_long_press(&input, &reader, 40, page_height() - 40);
      changed = true;
    } else if (cmd[0] == 't') {
      int x = 0, y = 0;
      if (std::sscanf(cmd.c_str(), "t %d %d", &x, &y) == 2) {
        synth_tap(&input, &reader, x, y);
        changed = true;
      } else {
        std::printf("  usage: t <x> <y>\n");
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
