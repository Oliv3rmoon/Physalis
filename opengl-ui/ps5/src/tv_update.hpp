// ProsperoTV - Updates on the console: the catalog check and the app replacing itself.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

namespace tv
{

// Asks GitHub whether Oliv3rmoon/Physalis has a newer release (the build's
// TV_UPDATE_REPOSITORY), on a thread of its own, when a check is due: the first
// call of a launch, then once every few hours of uptime
// (update_kit/update_schedule.h). Call it at launch and from the menu's frame
// loop, never from playback, so a check never begins during playback. The
// interface hears the answer through ptv::platform::update_take(), and a version
// is offered once a launch.
void start_update_check();

} // namespace tv
