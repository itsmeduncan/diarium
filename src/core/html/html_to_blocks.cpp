#include "core/html/html_to_blocks.h"

#include "core/base/str.h"
#include "core/xml/entities.h"

namespace diarium {
namespace {

bool is_ws(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

bool is_alpha(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool is_name_char(char c) {
  return is_alpha(c) || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
         c == ':';
}

// Tags whose entire contents are discarded.
bool is_dropped(const std::string& t) {
  return t == "script" || t == "style" || t == "noscript" || t == "iframe" ||
         t == "svg" || t == "form" || t == "button" || t == "select" ||
         t == "template" || t == "head" || t == "object" || t == "video" ||
         t == "audio" || t == "canvas" || t == "map" || t == "aside";
}

// Tags that force a block boundary but carry no styling of their own.
bool is_block_break(const std::string& t) {
  return t == "div" || t == "section" || t == "article" || t == "main" ||
         t == "header" || t == "footer" || t == "table" || t == "tr" ||
         t == "td" || t == "th" || t == "tbody" || t == "thead" ||
         t == "dl" || t == "dt" || t == "dd" || t == "address" ||
         t == "figure" || t == "center" || t == "fieldset";
}

uint8_t inline_flag_for(const std::string& t) {
  if (t == "b" || t == "strong") return kStyleBold;
  if (t == "i" || t == "em" || t == "cite" || t == "var" || t == "dfn")
    return kStyleItalic;
  if (t == "code" || t == "tt" || t == "kbd" || t == "samp") return kStyleCode;
  if (t == "a") return kStyleLink;
  return kStyleNone;
}

}  // namespace

HtmlToBlocks::HtmlToBlocks(BlockSink& sink, HtmlLimits limits)
    : sink_(sink), limits_(limits) {}

void HtmlToBlocks::reset() {
  state_ = State::Text;
  tag_.clear();
  attr_name_.clear();
  attr_value_.clear();
  attr_quote_ = '\0';
  self_closing_ = false;
  pending_attrs_src_.clear();
  pending_attrs_alt_.clear();
  comment_tail_.clear();
  raw_text_tag_.clear();
  raw_text_tail_.clear();
  in_entity_ = false;
  entity_.clear();
  current_ = Block();
  current_open_ = false;
  preserve_ws_ = false;
  pending_space_ = false;
  flags_ = kStyleNone;
  run_start_ = kRunPending;
  inline_stack_.clear();
  list_stack_.clear();
  quote_depth_ = 0;
  total_bytes_ = 0;
  blocks_emitted_ = 0;
  hit_limit_ = false;
  saw_markup_ = false;
}

bool HtmlToBlocks::at_block_limit() const {
  return blocks_emitted_ >= limits_.max_blocks ||
         total_bytes_ >= limits_.max_total_bytes;
}

void HtmlToBlocks::feed(const char* data, size_t n) {
  for (size_t i = 0; i < n; ++i) handle_char(data[i]);
}

void HtmlToBlocks::finish() {
  if (in_entity_) flush_entity();
  flush_block();
}

void HtmlToBlocks::handle_char(char c) {
  switch (state_) {
    case State::Text:
      if (c == '<') {
        state_ = State::TagStart;
        tag_.clear();
        self_closing_ = false;
      } else if (c == '&') {
        if (in_entity_) flush_entity();
        in_entity_ = true;
        entity_.clear();
      } else if (in_entity_) {
        if (c == ';') {
          uint32_t cp = 0;
          if (decode_entity(entity_, &cp)) {
            std::string utf8;
            utf8_append(utf8, cp);
            append_decoded(utf8);
          } else {
            append_decoded("&" + entity_ + ";");
          }
          in_entity_ = false;
          entity_.clear();
        } else if (is_ws(c) || entity_.size() >= 34) {
          flush_entity();
          append_text(c);
        } else {
          entity_.push_back(c);
        }
      } else {
        append_text(c);
      }
      break;

    case State::TagStart:
      if (in_entity_) flush_entity();
      if (c == '/') {
        state_ = State::EndTagName;
        tag_.clear();
      } else if (c == '!') {
        state_ = State::Bang;
        comment_tail_.clear();
      } else if (is_alpha(c)) {
        state_ = State::TagName;
        tag_.assign(1, ascii_lower(c));
      } else {  // a stray '<' in prose
        append_text('<');
        append_text(c);
        state_ = State::Text;
      }
      break;

    case State::TagName:
      if (is_name_char(c)) {
        if (tag_.size() < 24) tag_.push_back(ascii_lower(c));
      } else if (c == '>') {
        on_start_tag();
      } else if (c == '/') {
        self_closing_ = true;
        state_ = State::InTag;
      } else {
        state_ = State::InTag;
        attr_name_.clear();
      }
      break;

    case State::EndTagName:
      if (is_name_char(c)) {
        if (tag_.size() < 24) tag_.push_back(ascii_lower(c));
      } else if (c == '>') {
        on_end_tag(tag_);
        state_ = State::Text;
      }
      break;

    case State::InTag:
      if (c == '>') {
        on_start_tag();
      } else if (c == '/') {
        self_closing_ = true;
      } else if (is_alpha(c)) {
        state_ = State::AttrName;
        attr_name_.assign(1, ascii_lower(c));
        attr_value_.clear();
      }
      break;

    case State::AttrName:
      if (is_name_char(c)) {
        if (attr_name_.size() < 24) attr_name_.push_back(ascii_lower(c));
      } else if (c == '=') {
        state_ = State::AttrEquals;
      } else if (c == '>') {
        on_start_tag();
      } else {
        state_ = State::InTag;
      }
      break;

    case State::AttrEquals:
      if (is_ws(c)) break;
      if (c == '"' || c == '\'') {
        attr_quote_ = c;
        attr_value_.clear();
        state_ = State::AttrValue;
      } else if (c == '>') {
        on_start_tag();
      } else {
        attr_quote_ = '\0';
        attr_value_.assign(1, c);
        state_ = State::AttrValue;
      }
      break;

    case State::AttrValue: {
      const bool done = attr_quote_ ? (c == attr_quote_) : (is_ws(c) || c == '>');
      if (!done) {
        if (attr_value_.size() < 512) attr_value_.push_back(c);
        break;
      }
      if (attr_name_ == "src" || attr_name_ == "data-src") {
        pending_attrs_src_ = decode_entities(attr_value_);
      } else if (attr_name_ == "alt") {
        pending_attrs_alt_ = decode_entities(attr_value_);
      }
      attr_name_.clear();
      attr_value_.clear();
      if (!attr_quote_ && c == '>') {
        on_start_tag();
      } else {
        state_ = State::InTag;
      }
      break;
    }

    case State::Bang:
      comment_tail_.push_back(c);
      if (comment_tail_ == "--") {
        state_ = State::Comment;
        comment_tail_.clear();
      } else if (c == '>') {
        state_ = State::Text;  // doctype or other declaration
      } else if (comment_tail_.size() > 2) {
        // "<!DOCTYPE …" and friends: run to the next '>'.
        if (c == '>') state_ = State::Text;
        comment_tail_.clear();
        comment_tail_.push_back(c);
      }
      break;

    case State::Comment:
      comment_tail_.push_back(c);
      if (comment_tail_.size() > 3) comment_tail_.erase(0, 1);
      if (comment_tail_ == "-->") {
        state_ = State::Text;
        comment_tail_.clear();
      }
      break;

    case State::RawText: {
      raw_text_tail_.push_back(ascii_lower(c));
      const std::string close = "</" + raw_text_tag_ + ">";
      if (raw_text_tail_.size() > close.size()) raw_text_tail_.erase(0, 1);
      if (raw_text_tail_ == close) {
        state_ = State::Text;
        raw_text_tail_.clear();
        raw_text_tag_.clear();
      }
      break;
    }
  }
}

void HtmlToBlocks::flush_entity() {
  if (!in_entity_) return;
  append_decoded("&" + entity_);
  in_entity_ = false;
  entity_.clear();
}

void HtmlToBlocks::append_text(char c) {
  if (preserve_ws_) {
    if (!current_open_) begin_block(BlockType::Code, 0);
    if (current_.text.size() < limits_.max_block_bytes) {
      if (flags_ != kStyleNone && run_start_ == kRunPending) {
        run_start_ = current_.text.size();
      }
      current_.text.push_back(c);
      ++total_bytes_;
    }
    return;
  }
  if (is_ws(c)) {
    // Leading whitespace in a block is dropped; interior runs collapse to one
    // space, emitted lazily so trailing whitespace never lands in the block.
    if (current_open_ && !current_.text.empty()) pending_space_ = true;
    return;
  }
  if (!current_open_) begin_block(BlockType::Paragraph, 0);
  if (pending_space_) {
    if (current_.text.size() < limits_.max_block_bytes) {
      current_.text.push_back(' ');
      ++total_bytes_;
    }
    pending_space_ = false;
  }
  // The run opens at the first real character after any deferred space, so
  // the space stays outside it.
  if (flags_ != kStyleNone && run_start_ == kRunPending) {
    run_start_ = current_.text.size();
  }
  if (current_.text.size() < limits_.max_block_bytes) {
    current_.text.push_back(c);
    ++total_bytes_;
  } else {
    hit_limit_ = true;
  }
}

void HtmlToBlocks::append_decoded(const std::string& raw) {
  for (char c : raw) {
    // A decoded NBSP is a space for layout purposes, but it must not be
    // dropped as leading whitespace the way a literal space would be.
    append_text(c);
  }
}

void HtmlToBlocks::set_flags(uint8_t flags) {
  if (flags == flags_) return;
  if (flags_ != kStyleNone && run_start_ != kRunPending &&
      current_.text.size() > run_start_ &&
      current_.runs.size() < limits_.max_runs_per_block) {
    current_.runs.push_back(
        StyleRun{static_cast<uint32_t>(run_start_),
                 static_cast<uint32_t>(current_.text.size() - run_start_),
                 flags_});
  }
  run_start_ = kRunPending;
  flags_ = flags;
}

uint8_t HtmlToBlocks::flags_from_stack() const {
  uint8_t f = kStyleNone;
  for (const InlineTag& t : inline_stack_) f = static_cast<uint8_t>(f | t.flag);
  return f;
}

void HtmlToBlocks::push_inline(const std::string& tag, uint8_t flag) {
  inline_stack_.push_back(InlineTag{tag, flag});
  set_flags(flags_from_stack());
}

void HtmlToBlocks::pop_inline(const std::string& tag) {
  for (size_t i = inline_stack_.size(); i-- > 0;) {
    if (inline_stack_[i].tag == tag) {
      inline_stack_.erase(inline_stack_.begin() +
                          static_cast<std::ptrdiff_t>(i));
      break;
    }
  }
  set_flags(flags_from_stack());
}

void HtmlToBlocks::rebase_runs(size_t removed_from_front) {
  const size_t len = current_.text.size();
  auto shift = [&](size_t v) { return v > removed_from_front ? v - removed_from_front : 0; };

  if (run_start_ != kRunPending) {
    run_start_ = shift(run_start_);
    if (run_start_ > len) run_start_ = len;
  }
  if (current_.runs.empty()) return;

  std::vector<StyleRun> kept;
  kept.reserve(current_.runs.size());
  for (StyleRun r : current_.runs) {
    size_t start = shift(r.start);
    size_t end = shift(static_cast<size_t>(r.start) + r.length);
    if (start > len) start = len;
    if (end > len) end = len;
    if (end > start) {
      r.start = static_cast<uint32_t>(start);
      r.length = static_cast<uint32_t>(end - start);
      kept.push_back(r);
    }
  }
  current_.runs.swap(kept);
}

void HtmlToBlocks::begin_block(BlockType type, uint8_t level) {
  current_ = Block();
  current_.type = type;
  current_.level = level;
  current_open_ = true;
  pending_space_ = false;
  run_start_ = kRunPending;
  flags_ = flags_from_stack();
  // Paragraphs inside a <blockquote> are quoted text, not body text — the
  // quote element itself carries no content of its own in real-world HTML.
  if (quote_depth_ > 0 && (type == BlockType::Paragraph ||
                           type == BlockType::Blockquote)) {
    current_.type = BlockType::Blockquote;
    current_.level = quote_depth_;
  }
}

void HtmlToBlocks::flush_block() {
  if (!current_open_) return;

  // Trim before closing the final run, not after: a run closed against the
  // untrimmed length would overhang the text and the renderer would read past
  // the end of the string. Trimming the *front* — which only Code blocks do —
  // moves every existing offset, so the runs are rebased to match.
  pending_space_ = false;
  size_t lead = 0;
  if (preserve_ws_) {
    while (lead < current_.text.size() && is_ws(current_.text[lead])) ++lead;
    size_t end = current_.text.size();
    while (end > lead && is_ws(current_.text[end - 1])) --end;
    current_.text = current_.text.substr(lead, end - lead);
  } else {
    while (!current_.text.empty() && current_.text.back() == ' ') {
      current_.text.pop_back();
    }
  }
  rebase_runs(lead);

  set_flags(kStyleNone);  // closes any open run
  flags_ = flags_from_stack();
  current_open_ = false;

  if (current_.text.empty() && current_.type != BlockType::Rule &&
      current_.type != BlockType::Image) {
    current_ = Block();
    return;
  }
  if (blocks_emitted_ >= limits_.max_blocks) {
    hit_limit_ = true;
    current_ = Block();
    return;
  }
  ++blocks_emitted_;
  sink_.on_block(std::move(current_));
  current_ = Block();
  run_start_ = kRunPending;
}

void HtmlToBlocks::on_start_tag() {
  const std::string tag = tag_;
  const bool self_closing = self_closing_;
  const std::string src = pending_attrs_src_;
  const std::string alt = pending_attrs_alt_;
  pending_attrs_src_.clear();
  pending_attrs_alt_.clear();
  self_closing_ = false;
  state_ = State::Text;
  saw_markup_ = true;

  if (is_dropped(tag)) {
    if (!self_closing) {
      state_ = State::RawText;
      raw_text_tag_ = tag;
      raw_text_tail_.clear();
    }
    return;
  }

  if (tag == "br") {
    if (current_open_ && !current_.text.empty()) {
      current_.text.push_back('\n');
      pending_space_ = false;
    }
    return;
  }
  if (tag == "hr") {
    flush_block();
    if (!at_block_limit()) {
      Block b;
      b.type = BlockType::Rule;
      ++blocks_emitted_;
      sink_.on_block(std::move(b));
    }
    return;
  }
  if (tag == "img") {
    // v1 renders a labelled placeholder; the seam for real image decoding is
    // here and in the page renderer, nowhere else.
    flush_block();
    if (!at_block_limit() && (!src.empty() || !alt.empty())) {
      Block b;
      b.type = BlockType::Image;
      b.text = collapse_ws(alt);
      b.src = src;
      ++blocks_emitted_;
      sink_.on_block(std::move(b));
    }
    return;
  }

  const uint8_t inline_flag = inline_flag_for(tag);
  if (inline_flag != kStyleNone) {
    if (!current_open_) begin_block(BlockType::Paragraph, 0);
    push_inline(tag, inline_flag);
    return;
  }

  if (tag == "p") {
    flush_block();
    begin_block(BlockType::Paragraph, 0);
    return;
  }
  if (tag.size() == 2 && tag[0] == 'h' && tag[1] >= '1' && tag[1] <= '6') {
    flush_block();
    begin_block(BlockType::Heading, static_cast<uint8_t>(tag[1] - '0'));
    return;
  }
  if (tag == "blockquote") {
    flush_block();
    if (quote_depth_ < 4) ++quote_depth_;
    begin_block(BlockType::Blockquote, quote_depth_);
    return;
  }
  if (tag == "ul" || tag == "ol") {
    flush_block();
    if (list_stack_.size() < 6) {
      list_stack_.push_back(ListLevel{tag == "ol", 0});
    }
    return;
  }
  if (tag == "li") {
    flush_block();
    const bool ordered = !list_stack_.empty() && list_stack_.back().ordered;
    uint16_t index = 0;
    if (!list_stack_.empty()) index = ++list_stack_.back().index;
    begin_block(BlockType::ListItem,
                static_cast<uint8_t>(list_stack_.empty() ? 1
                                                         : list_stack_.size()));
    current_.ordered = ordered;
    current_.list_index = index;
    return;
  }
  if (tag == "pre") {
    flush_block();
    preserve_ws_ = true;
    begin_block(BlockType::Code, 0);
    return;
  }
  if (tag == "figcaption") {
    flush_block();
    begin_block(BlockType::Caption, 0);
    return;
  }
  if (is_block_break(tag)) {
    flush_block();
    return;
  }
  // Unknown tag: treat as transparent.
}

void HtmlToBlocks::on_end_tag(const std::string& tag) {
  saw_markup_ = true;
  if (is_dropped(tag)) return;

  const uint8_t inline_flag = inline_flag_for(tag);
  if (inline_flag != kStyleNone) {
    pop_inline(tag);
    return;
  }
  if (tag == "pre") {
    flush_block();
    preserve_ws_ = false;
    return;
  }
  if (tag == "blockquote") {
    flush_block();
    if (quote_depth_ > 0) --quote_depth_;
    return;
  }
  if (tag == "ul" || tag == "ol") {
    flush_block();
    if (!list_stack_.empty()) list_stack_.pop_back();
    return;
  }
  if (tag == "p" || tag == "li" || tag == "figcaption" ||
      (tag.size() == 2 && tag[0] == 'h' && tag[1] >= '1' && tag[1] <= '6') ||
      is_block_break(tag)) {
    flush_block();
    return;
  }
}

std::vector<Block> html_to_blocks(const std::string& html, HtmlLimits limits) {
  BlockCollector collector;
  HtmlToBlocks conv(collector, limits);
  conv.feed(html);
  conv.finish();
  return std::move(collector.blocks);
}

std::vector<Block> text_to_blocks(const std::string& text) {
  std::vector<Block> out;
  std::string para;
  size_t blank_run = 0;
  auto flush = [&]() {
    const std::string t = collapse_ws(para);
    if (!t.empty()) {
      Block b;
      b.type = BlockType::Paragraph;
      b.text = t;
      out.push_back(std::move(b));
    }
    para.clear();
  };
  for (size_t i = 0; i < text.size(); ++i) {
    const char c = text[i];
    if (c == '\n') {
      if (++blank_run >= 2) flush();
    } else if (!is_ws(c)) {
      blank_run = 0;
    }
    para.push_back(c);
  }
  flush();
  return out;
}

}  // namespace diarium
