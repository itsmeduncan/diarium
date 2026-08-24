// diarium-sim — runs the whole device pipeline on a desktop.
//
// The point of this binary is that layout is the product, and layout needs to
// be looked at. Everything above the HAL is the same code the Inkplate runs;
// only the display, input, clock and network are faked.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "sim/commands.h"

namespace {

void usage() {
  std::fprintf(stderr,
               "diarium-sim — desktop harness for the Diarium firmware\n"
               "\n"
               "  parse    <feed.xml>...            parse feeds, report what "
               "came out\n"
               "  compose  [--config F] [--out D]   build an edition, write "
               "page PNGs\n"
               "  read     [--config F] [--out D]   interactive reader over a "
               "composed edition\n"
               "  opml     <subs.opml>...           import subscriptions as "
               "feeds.toml\n"
               "\n"
               "Common flags:\n"
               "  --fonts  PATH   font pack (default build/literata.rfp)\n"
               "  --out    DIR    output directory (default out)\n"
               "  --config PATH   feed config (default config/feeds.toml)\n");
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    usage();
    return 2;
  }
  const std::string cmd = argv[1];
  std::vector<std::string> args(argv + 2, argv + argc);

  if (cmd == "parse") return diarium::sim::cmd_parse(args);
  if (cmd == "compose") return diarium::sim::cmd_compose(args);
  if (cmd == "read") return diarium::sim::cmd_read(args);
  if (cmd == "opml") return diarium::sim::cmd_opml(args);
  if (cmd == "screens") return diarium::sim::cmd_screens(args);
  if (cmd == "-h" || cmd == "--help" || cmd == "help") {
    usage();
    return 0;
  }

  std::fprintf(stderr, "diarium-sim: unknown command '%s'\n\n", cmd.c_str());
  usage();
  return 2;
}
