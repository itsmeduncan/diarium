// The page the reader gets when there is nothing to read: no card, an
// unreadable card, no font pack.
//
// A notice is a page rather than an error, because these are normal states.
// The first card this project ever used was formatted HFS+ — readable at
// block level, unmountable — and a reader holding a blank panel deserves to
// be told which of those happened.
#pragma once

#include <string>

#include "core/render/framebuffer.h"
#include "core/text/font_pack.h"

namespace diarium {

// Draws over whatever was there. Degrades to a rule and a mark when `fonts`
// has no faces, which is exactly the case when the font pack is what is
// missing.
void render_notice(const FontPack& fonts, const char* headline,
                   const char* body, Framebuffer* fb);

// What the panel holds while the device sleeps. E-ink keeps its last image,
// so this is what the paper looks like sitting on a table — it should look
// like a paper at rest, not like a device that has switched off.
void render_sleep_page(const FontPack& fonts, const std::string& title,
                       const std::string& date_line, Framebuffer* fb);

}  // namespace diarium
