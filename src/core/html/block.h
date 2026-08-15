// The block model: the only representation of article content that the layout
// engine ever sees. HTML dies at this boundary — nothing downstream knows what
// a <div> is, which is what lets the same layout code serve a future Markdown
// or plain-text source.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace rsspaper {

enum class BlockType : uint8_t {
  Paragraph,
  Heading,     // `level` 1-6
  Blockquote,  // `level` = nesting depth, 1-based
  ListItem,    // `level` = nesting depth, `ordered` + `list_index` for markers
  Code,        // whitespace preserved
  Rule,        // horizontal rule, no text
  Image,       // placeholder in v1: `text` holds the alt, `src` the URL
  Caption,     // figcaption and friends
};

// Inline styling. Runs are non-overlapping and cover only styled spans; text
// outside every run is plain body.
enum StyleFlag : uint8_t {
  kStyleNone = 0,
  kStyleBold = 1 << 0,
  kStyleItalic = 1 << 1,
  kStyleCode = 1 << 2,
  kStyleLink = 1 << 3,
};

struct StyleRun {
  uint32_t start = 0;   // byte offset into Block::text
  uint32_t length = 0;  // in bytes
  uint8_t flags = kStyleNone;
};

struct Block {
  BlockType type = BlockType::Paragraph;
  uint8_t level = 0;
  bool ordered = false;
  uint16_t list_index = 0;
  // UTF-8, whitespace-collapsed except in Code blocks. A '\n' is a hard break
  // requested by the source (<br>), not a paragraph boundary.
  std::string text;
  std::vector<StyleRun> runs;
  std::string src;  // Image only

  bool empty() const { return text.empty() && type != BlockType::Rule; }
};

class BlockSink {
 public:
  virtual ~BlockSink() = default;
  virtual void on_block(Block&& block) = 0;
};

// Collects into a vector. The common case outside of tests.
class BlockCollector final : public BlockSink {
 public:
  void on_block(Block&& block) override { blocks.push_back(std::move(block)); }
  std::vector<Block> blocks;
};

}  // namespace rsspaper
