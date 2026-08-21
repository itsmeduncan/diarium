#include "core/feed/item.h"

#include "core/base/str.h"

namespace diarium {

const char* truncation_reason_name(TruncationReason r) {
  switch (r) {
    case TruncationReason::None: return "none";
    case TruncationReason::SummaryOnly: return "summary-only";
    case TruncationReason::EllipsisTail: return "ellipsis-tail";
    case TruncationReason::ReadMoreLink: return "read-more-link";
    case TruncationReason::VeryShort: return "very-short";
  }
  return "none";
}

uint64_t Item::dedup_key() const {
  // guid first: it's the only field a publisher promises to keep stable.
  if (!guid.empty()) return fnv1a("g:" + guid);
  if (!link.empty()) return fnv1a("l:" + link);
  return fnv1a("t:" + title + "|" + std::to_string(published));
}

}  // namespace diarium
