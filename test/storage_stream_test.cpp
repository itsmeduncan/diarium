// The storage primitives the streaming writer and the lazy reader need:
// open a path for chunked writes, and read a file back in ranges rather than
// whole. Exercised against SimStorage, the desktop backing — DeviceStorage's
// mirror image is proven on the board in Task 5.
#include <sys/stat.h>

#include <memory>
#include <string>

#include "core/io/byte_sink.h"
#include "doctest.h"
#include "hal/hal.h"
#include "sim/sim_storage.h"

using namespace diarium;
using diarium::sim::SimStorage;

namespace {

// SimStorage resolves paths under a root directory it does not create
// itself — match how the interactive tools set one up (ensure_output_dir)
// by making it ourselves before each test.
std::string temp_root() {
  const std::string root = "out/storage_stream_test";
  ::mkdir("out", 0755);
  ::mkdir(root.c_str(), 0755);
  return root;
}

}  // namespace

TEST_CASE("open_write streams a file in chunks that size() and read_range see") {
  SimStorage storage(temp_root());

  std::unique_ptr<ByteSink> sink = storage.open_write("streamed.bin");
  REQUIRE(sink != nullptr);

  const std::string part_a = "the first chunk of the story";
  const std::string part_b = ", and the second chunk that follows it.";
  REQUIRE(sink->write(part_a.data(), part_a.size()));
  REQUIRE(sink->write(part_b.data(), part_b.size()));
  CHECK(sink->position() == part_a.size() + part_b.size());
  CHECK(sink->ok());

  sink.reset();  // destroying the sink closes the file

  const std::string whole = part_a + part_b;
  CHECK(storage.size("streamed.bin") == whole.size());

  std::string mid;
  REQUIRE(storage.read_range("streamed.bin", part_a.size(), part_b.size(), &mid));
  CHECK(mid == part_b);

  std::string all;
  REQUIRE(storage.read_range("streamed.bin", 0, whole.size(), &all));
  CHECK(all == whole);
}

TEST_CASE("read_range refuses a range that runs past the file") {
  SimStorage storage(temp_root());
  REQUIRE(storage.write("short.bin", "twelve bytes"));  // 12 bytes

  std::string out;
  CHECK_FALSE(storage.read_range("short.bin", 20, 4, &out));   // offset past EOF
  CHECK_FALSE(storage.read_range("short.bin", 8, 100, &out));  // runs past EOF

  // Reading exactly to the end is in bounds.
  CHECK(storage.read_range("short.bin", 8, 4, &out));
  CHECK(out == "ytes");
}

TEST_CASE("size and read_range are honest about an absent file") {
  SimStorage storage(temp_root());
  CHECK(storage.size("nope.bin") == 0);

  std::string out;
  CHECK_FALSE(storage.read_range("nope.bin", 0, 1, &out));
}

TEST_CASE("open_write truncates whatever was there before") {
  SimStorage storage(temp_root());
  REQUIRE(storage.write("truncate.bin", "a much longer first version of this file"));

  std::unique_ptr<ByteSink> sink = storage.open_write("truncate.bin");
  REQUIRE(sink != nullptr);
  const std::string shorter = "short";
  REQUIRE(sink->write(shorter.data(), shorter.size()));
  sink.reset();

  CHECK(storage.size("truncate.bin") == shorter.size());
  std::string out;
  REQUIRE(storage.read_range("truncate.bin", 0, shorter.size(), &out));
  CHECK(out == shorter);
}
