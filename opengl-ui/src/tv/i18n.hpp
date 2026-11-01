// ProsperoTV - Menu translations selected from the console language.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <string_view>

namespace ptv
{
// Call at startup before constructing screens. Unknown languages use English.
void set_console_language(int language);
const char *interface_language();
// 0 follows the console; 1..9 select en/es/fr/de/it/pt/nl/ar/he.
void set_interface_language(unsigned choice);
bool interface_rtl();
// Only presentation text belongs here, never source IDs or provider metadata.
const char *tr(const char *english);
// The player's reason for a channel that did not play, "Channel request failed: <what
// happened> (curl 28).", in the interface's language when both its parts have a translation;
// otherwise as the player wrote it, not half translated.
std::string tr_reason(std::string_view english);
} // namespace ptv
