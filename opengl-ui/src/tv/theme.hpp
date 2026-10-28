// ProsperoTV - Nightshade: the app's design language as kit tokens.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "tv/kit.hpp"
#include "tv/settings.hpp"

namespace ptv
{

// The colours the screens name directly. Everything else comes from the theme.
// Nightshade TV: Nightshade's own palette (its "nightly" theme, Oliv3rmoon/Nightshade
// headless/prosperoeden/pe/ui/theme.cpp): black and red, clean and dark.
namespace tone
{
inline const Color night = Color::rgb(0x09090b);     // the page: near black (base)
inline const Color panel = Color::rgb(0x0e0e11);     // panels and tiles (panel)
inline const Color panel_lit = Color::rgb(0x26161a); // the tile in focus (row_focus)
inline const Color cream = Color::rgb(0xededf0);     // text, and the light actions (text)
inline const Color ink = Color::rgb(0x0b0b0d);       // text on cream and on the accent (on_accent)
inline const Color accent = Color::rgb(0xe5484d);    // red: one per region (accent)
inline const Color ember = Color::rgb(0xff6b72);     // the light the focus gives off (highlight)
inline const Color wine = Color::rgb(0x5a1419);      // the dark half of that light (accent_deep)
inline const Color good = Color::rgb(0x6fe3ae);
inline const Color bad = Color::rgb(0xff7a7a);
inline const Color wait = Color::rgb(0xf0a35e); // amber: never the accent's red (warning)
} // namespace tone

// Adapt the app-owned frame without changing channel images or video textures.
void apply_appearance(Frame &frame, const Settings &settings);
// Change font metrics before loading; measurement, fitting and drawing then agree.
bool load_menu_font(std::string data, gfx::Font &font, bool larger);

// Nightshade: black glass over a near-black sky with a low red glow, as the kit's
// frosted-glass recipe.
ui::Theme dusk();

// The sky behind everything. `lean` and `lean_dark` are the colours of the
// channel in focus and `amount` (0..1) how far the clouds take them.
gfx::BackdropSpec dusk_sky(Color lean, Color lean_dark, float amount, float time);

} // namespace ptv
