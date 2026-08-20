// `rsspaper-sim screens` — renders the screens that are not composed pages.
//
// The sleep screen, the notices, and the end of the news are all drawn by the
// device rather than laid out by the composer, which means they are the ones
// nobody ever looks at until the panel shows them. This makes them
// inspectable on a laptop like everything else.
#include <cstdio>
#include <string>
#include <vector>

#include "core/base/datetime.h"
#include "core/edition/edition_store.h"
#include "core/render/framebuffer.h"
#include "core/text/font_pack.h"
#include "core/ui/notice.h"
#include "sim/commands.h"
#include "sim/fixtures.h"
#include "sim/png_writer.h"

namespace rsspaper {
namespace sim {

int cmd_screens(const std::vector<std::string>& args) {
  const std::string fonts_path = flag(args, "--fonts", "build/literata.rfp");
  const std::string out_dir = flag(args, "--out", "out/screens");
  const std::string title = flag(args, "--title", "RSSpaper");

  FontPack fonts;
  std::string error;
  if (!fonts.load_file(fonts_path, &error)) {
    std::fprintf(stderr, "screens: %s\n", error.c_str());
    return 1;
  }
  ensure_output_dir(out_dir);

  const Depth depth = has_flag(args, "--mono") ? Depth::Mono1 : Depth::Grey3;
  const std::string date_line = format_masthead_date(1786864000);

  struct Screen {
    const char* name;
    const char* headline;
    const char* body;
  };
  const Screen notices[] = {
      {"no-card", "No card",
       "Insert a card with feeds.toml and an edition on it."},
      {"card-unreadable", "Card unreadable",
       "The card answers but its filesystem does not. Format it as exFAT."},
      {"no-paper", "No paper yet",
       "The card has no edition on it. Compose one and copy it over."},
  };

  Framebuffer fb;
  for (const Screen& s : notices) {
    render_notice(fonts, s.headline, s.body, &fb);
    const std::string path = out_dir + "/" + s.name + ".png";
    if (!write_png(fb, depth, path)) {
      std::fprintf(stderr, "screens: cannot write %s\n", path.c_str());
      return 1;
    }
    std::printf("  %s\n", path.c_str());
  }

  render_sleep_page(fonts, title, date_line, &fb);
  const std::string sleep_path = out_dir + "/sleep.png";
  if (!write_png(fb, depth, sleep_path)) {
    std::fprintf(stderr, "screens: cannot write %s\n", sleep_path.c_str());
    return 1;
  }
  std::printf("  %s\n", sleep_path.c_str());
  return 0;
}

}  // namespace sim
}  // namespace rsspaper
