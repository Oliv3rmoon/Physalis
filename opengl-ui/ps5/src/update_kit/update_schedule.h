/*
 * Physalis - When the app asks GitHub again for a newer release, and which answers it shows.
 * SPDX-FileCopyrightText: Copyright 2026 Oliv3rmoon
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The check (github_release.h) ran once, at launch, so a console left on for days never heard
 * of a release made after it started. These are the rules for asking again, kept apart from the
 * network so the host tests can hold them to their word (opengl-ui/tests/update_schedule_check.cc,
 * run by tests/test_github_release.py):
 *
 *   1. The first check of a launch begins at once.
 *   2. Later checks begin once UPDATE_SCHEDULE_INTERVAL_US of uptime has passed since the last
 *      one began, whatever it answered. GitHub lets one address ask 60 times an hour without an
 *      account, and a household's consoles share an address: a check every few hours, and never
 *      a quicker retry after a failure (no network, a refusal, the rate limit), keeps every
 *      console far inside that and costs the server next to nothing. Each console counts its own
 *      uptime, so consoles started at different times never ask together.
 *   3. Only the menu asks whether one is due (tv::start_update_check() in its frame loop), so a
 *      check never begins during playback, and one due during playback begins when the viewer
 *      comes back to the menu: at most once an interval. The answer is taken only by the menu
 *      too (ptv::platform::update_take()), so it is never shown over a picture.
 *   4. Never while a check is under way.
 *   5. A version is offered at most once a launch: a release the viewer skipped is not asked
 *      about again until the app starts again, and only a newer one is offered meanwhile.
 *
 * Times are the console's monotonic clock in microseconds. This file is C11, holds no console
 * calls and no allocation; the caller serializes the calls.
 */
#ifndef PHYSALIS_UPDATE_SCHEDULE_H
#define PHYSALIS_UPDATE_SCHEDULE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* Four hours of uptime between checks. */
#define UPDATE_SCHEDULE_INTERVAL_US (UINT64_C(4) * 60u * 60u * 1000000u)
/* The versions a launch remembers having offered; the oldest is forgotten first. */
#define UPDATE_SCHEDULE_OFFERED 8u
/* A content version, "01.002.005", and its terminating zero, with room to spare. */
#define UPDATE_SCHEDULE_VERSION_BYTES 16u

typedef struct update_schedule
{
    uint64_t last_us; /* when the last check began */
    int begun;        /* a check has begun this launch */
    int running;      /* a check is under way */
    unsigned offered_count;
    char offered[UPDATE_SCHEDULE_OFFERED][UPDATE_SCHEDULE_VERSION_BYTES];
} update_schedule;

void update_schedule_init(update_schedule *schedule);
/* 1 when a check should begin at now_us (rules 1, 2 and 4), else 0. A clock that went back
 * counts as a new start: at most one extra check. */
int update_schedule_due(const update_schedule *schedule, uint64_t now_us);
/* A check began at now_us. */
void update_schedule_begin(update_schedule *schedule, uint64_t now_us);
/* The check that began has ended, with or without an answer. */
void update_schedule_end(update_schedule *schedule);
/* A check found `available` (its content version): 1 when it is shown, the first time this
 * launch that version was found; 0 when it was already offered, or is empty or too long to be
 * a content version. A shown version is remembered (rule 5). */
int update_schedule_offer(update_schedule *schedule, const char *available);

#ifdef __cplusplus
}
#endif

#endif
