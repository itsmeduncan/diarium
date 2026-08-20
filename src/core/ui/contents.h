// The page you land on: the running order of the news you have not read.
//
// It is drawn rather than composed, because it is a view of what is left
// rather than a fact about the edition — it changes as you read, and the
// edition does not. A paper's front page is a contents page that orders by
// importance; this one orders by time, because that is the order you will
// walk it in.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "core/edition/edition.h"
#include "core/render/framebuffer.h"
#include "core/text/font_pack.h"

namespace rsspaper {

// `order` is story indices oldest-first; `unread` is the same length and says
// which are still to read. Nothing here counts what is outstanding — the list
// is as long as the page allows and stops.
void render_contents(const FontPack& fonts, const Edition& edition,
                     const std::vector<size_t>& order,
                     const std::vector<bool>& unread, const std::string& strap,
                     Framebuffer* fb);

}  // namespace rsspaper
