// Entity decoding shared by the XML and HTML paths. Feeds routinely contain
// HTML-only entities inside XML text nodes, so one table serves both.
#pragma once

#include <cstdint>
#include <string>

namespace rsspaper {

// Resolves the body of an entity reference (what sits between '&' and ';'),
// including numeric forms "#160" and "#xA0". Returns false if unknown, in
// which case the caller should emit the raw text — real feeds contain bare
// ampersands and swallowing them loses content.
bool decode_entity(const std::string& body, uint32_t* out_cp);

// Decodes every entity in `s` in place-ish, returning the decoded string.
std::string decode_entities(const std::string& s);

}  // namespace rsspaper
