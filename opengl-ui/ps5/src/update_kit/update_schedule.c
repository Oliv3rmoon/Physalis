/*
 * Physalis - When the app asks GitHub again for a newer release, and which answers it shows.
 * SPDX-FileCopyrightText: Copyright 2026 Oliv3rmoon
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * See update_schedule.h for the rules and why.
 */
#include "update_schedule.h"

#include <string.h>

void update_schedule_init(update_schedule *schedule)
{
    if (schedule != NULL)
        memset(schedule, 0, sizeof(*schedule));
}

int update_schedule_due(const update_schedule *schedule, uint64_t now_us)
{
    if (schedule == NULL || schedule->running)
        return 0;
    if (!schedule->begun || now_us < schedule->last_us)
        return 1;
    return now_us - schedule->last_us >= UPDATE_SCHEDULE_INTERVAL_US;
}

void update_schedule_begin(update_schedule *schedule, uint64_t now_us)
{
    if (schedule == NULL)
        return;
    schedule->last_us = now_us;
    schedule->begun = 1;
    schedule->running = 1;
}

void update_schedule_end(update_schedule *schedule)
{
    if (schedule != NULL)
        schedule->running = 0;
}

int update_schedule_offer(update_schedule *schedule, const char *available)
{
    size_t length;
    unsigned index;

    if (schedule == NULL || available == NULL)
        return 0;
    length = strlen(available);
    if (length == 0 || length >= UPDATE_SCHEDULE_VERSION_BYTES)
        return 0;
    for (index = 0; index < schedule->offered_count; ++index)
        if (strcmp(schedule->offered[index], available) == 0)
            return 0;
    if (schedule->offered_count == UPDATE_SCHEDULE_OFFERED)
    {
        memmove(schedule->offered[0], schedule->offered[1],
                sizeof(schedule->offered[0]) * (UPDATE_SCHEDULE_OFFERED - 1u));
        --schedule->offered_count;
    }
    memcpy(schedule->offered[schedule->offered_count], available, length + 1u);
    ++schedule->offered_count;
    return 1;
}
