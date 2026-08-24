// Push-mode HTML → block converter.
//
// Push-mode (rather than pull, like the XML parser) because the feed parser is
// already pulling: content arrives as chunks of a text node and has to be
// consumed as it lands, without ever holding a whole article as one string.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "core/html/block.h"

namespace diarium {

struct HtmlLimits {
  size_t max_blocks = 600;
  size_t max_block_bytes = 8192;
  size_t max_total_bytes = 262144;  // 256 KB of text per item
  size_t max_runs_per_block = 128;
};

class HtmlToBlocks {
 public:
  HtmlToBlocks(BlockSink& sink, HtmlLimits limits = HtmlLimits());

  // Feed arbitrary chunks; boundaries may fall anywhere, including mid-tag or
  // mid-entity.
  void feed(const char* data, size_t n);
  void feed(const std::string& s) { feed(s.data(), s.size()); }

  // Flushes the trailing block. Required before reading results.
  void finish();

  // Discards all state. Used when a higher-priority content element shows up
  // after we already started building from a lower-priority one.
  void reset();

  bool hit_limit() const { return hit_limit_; }
  size_t text_bytes() const { return total_bytes_; }
  size_t blocks_emitted() const { return blocks_emitted_; }
  // True if the source contained at least one <a>, <p> or block tag — used to
  // tell real HTML from a plain-text summary that happens to have an
  // ampersand in it.
  bool saw_markup() const { return saw_markup_; }

 private:
  enum class State : uint8_t {
    Text,
    TagStart,
    TagName,
    EndTagName,
    InTag,
    AttrName,
    AttrEquals,
    AttrValue,
    Bang,
    Comment,
    RawText,  // inside <script>/<style>: text until the matching close tag
  };

  void handle_char(char c);
  void on_start_tag();
  void on_end_tag(const std::string& tag);
  void append_text(char c);
  void append_decoded(const std::string& raw);
  void flush_entity();
  void flush_block();
  // Re-anchors style runs after `removed_from_front` bytes were trimmed off
  // the head of the block text, dropping runs that no longer cover anything.
  void rebase_runs(size_t removed_from_front);
  void begin_block(BlockType type, uint8_t level);
  void set_flags(uint8_t flags);
  uint8_t flags_from_stack() const;
  void push_inline(const std::string& tag, uint8_t flag);
  void pop_inline(const std::string& tag);
  bool at_block_limit() const;

  BlockSink& sink_;
  HtmlLimits limits_;

  State state_ = State::Text;
  std::string tag_;
  std::string attr_name_;
  std::string attr_value_;
  char attr_quote_ = '\0';
  bool self_closing_ = false;
  std::string pending_attrs_src_;
  std::string pending_attrs_alt_;
  std::string comment_tail_;
  std::string raw_text_tag_;
  std::string raw_text_tail_;

  // Entity accumulation, which can straddle a chunk boundary.
  bool in_entity_ = false;
  std::string entity_;

  // A style run's start is fixed when the first real character arrives, not
  // when the tag opens — otherwise the space between "plain" and "<b>bold"
  // lands inside the bold run, and a trailing space before "</b>" lands
  // inside it too and then gets trimmed out from under it.
  static constexpr size_t kRunPending = static_cast<size_t>(-1);

  Block current_;
  bool current_open_ = false;
  bool preserve_ws_ = false;
  bool pending_space_ = false;
  uint8_t flags_ = kStyleNone;
  size_t run_start_ = kRunPending;

  struct InlineTag {
    std::string tag;
    uint8_t flag;
  };
  std::vector<InlineTag> inline_stack_;

  struct ListLevel {
    bool ordered;
    uint16_t index;
  };
  std::vector<ListLevel> list_stack_;
  uint8_t quote_depth_ = 0;

  size_t total_bytes_ = 0;
  size_t blocks_emitted_ = 0;
  bool hit_limit_ = false;
  bool saw_markup_ = false;
};

// Convenience for tests and for short strings already in memory.
std::vector<Block> html_to_blocks(const std::string& html,
                                  HtmlLimits limits = HtmlLimits());

// Plain text (a summary with no markup) split on blank lines into paragraphs.
std::vector<Block> text_to_blocks(const std::string& text);

}  // namespace diarium
