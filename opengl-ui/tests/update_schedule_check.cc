// Physalis - Host check of when the update check asks again, and which answers it shows.
// SPDX-FileCopyrightText: Copyright 2026 Oliv3rmoon
// SPDX-License-Identifier: GPL-3.0-or-later
//
// ps5/src/update_kit/update_schedule.{h,c}: a check at launch, then one every interval of
// uptime whatever the last one answered, never two at once, and a version offered at most once
// a launch. tests/test_github_release.py builds and runs it (make test-integration).
#include "update_kit/update_schedule.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace
{

constexpr std::uint64_t kSecond = 1000000u;
constexpr std::uint64_t kHour = 3600u * kSecond;

void launch_checks_at_once_then_every_interval()
{
    static_assert(UPDATE_SCHEDULE_INTERVAL_US == 4u * kHour, "every four hours of uptime");
    update_schedule schedule;
    update_schedule_init(&schedule);
    // A console that has been on for days before the app starts still checks at once.
    const std::uint64_t launch = 52u * kHour;
    assert(update_schedule_due(&schedule, 0) == 1);
    assert(update_schedule_due(&schedule, launch) == 1);
    update_schedule_begin(&schedule, launch);
    // Never two at once, however long the first takes.
    assert(update_schedule_due(&schedule, launch) == 0);
    assert(update_schedule_due(&schedule, launch + 10u * kHour) == 0);
    update_schedule_end(&schedule);
    // The menu asks every frame: nothing until four hours after the last check began.
    for (std::uint64_t now = launch; now < launch + UPDATE_SCHEDULE_INTERVAL_US;
         now += 16667u * 61u)
        assert(update_schedule_due(&schedule, now) == 0);
    assert(update_schedule_due(&schedule, launch + UPDATE_SCHEDULE_INTERVAL_US - 1u) == 0);
    assert(update_schedule_due(&schedule, launch + UPDATE_SCHEDULE_INTERVAL_US) == 1);
    std::printf("launch and interval PASS\n");
}

void a_failed_check_waits_as_long_as_an_answered_one()
{
    // No network: each check fails at once. A day of frames asks six times, not every frame.
    update_schedule schedule;
    update_schedule_init(&schedule);
    unsigned checks = 0;
    for (std::uint64_t now = kSecond; now <= kSecond + 24u * kHour; now += kSecond)
        if (update_schedule_due(&schedule, now))
        {
            update_schedule_begin(&schedule, now);
            update_schedule_end(&schedule); // failed straight away
            ++checks;
        }
    assert(checks == 7u); // at launch and at 4, 8, 12, 16, 20 and 24 hours
    std::printf("failures wait the interval PASS\n");
}

void a_check_due_during_playback_runs_once_back_at_the_menu()
{
    // Only the menu asks; a three-hour film started 2 h after the last check
    // keeps the menu away past the interval. Back at the menu: one check, then
    // none until the next interval from that one.
    update_schedule schedule;
    update_schedule_init(&schedule);
    update_schedule_begin(&schedule, kHour);
    update_schedule_end(&schedule);
    const std::uint64_t back = kHour + 2u * kHour + 3u * kHour;
    assert(update_schedule_due(&schedule, back) == 1);
    update_schedule_begin(&schedule, back);
    update_schedule_end(&schedule);
    assert(update_schedule_due(&schedule, back + kSecond) == 0);
    assert(update_schedule_due(&schedule, back + UPDATE_SCHEDULE_INTERVAL_US - kSecond) == 0);
    assert(update_schedule_due(&schedule, back + UPDATE_SCHEDULE_INTERVAL_US) == 1);
    // A clock that went back is a new start: one check, not none for ever.
    assert(update_schedule_due(&schedule, back - kHour) == 1);
    std::printf("back at the menu PASS\n");
}

void each_version_is_offered_once_a_launch()
{
    update_schedule schedule;
    update_schedule_init(&schedule);
    assert(update_schedule_offer(&schedule, "01.002.005") == 1);
    // The viewer chose Skip: the same release, found by every later check, is not asked again.
    for (int check = 0; check < 20; ++check)
        assert(update_schedule_offer(&schedule, "01.002.005") == 0);
    // A newer release made meanwhile is offered, once.
    assert(update_schedule_offer(&schedule, "01.002.006") == 1);
    assert(update_schedule_offer(&schedule, "01.002.006") == 0);
    assert(update_schedule_offer(&schedule, "01.002.005") == 0);
    // Nothing to offer: no version, or one that is no content version.
    assert(update_schedule_offer(&schedule, "") == 0);
    assert(update_schedule_offer(&schedule, nullptr) == 0);
    assert(update_schedule_offer(&schedule, "01.002.005-and-much-more") == 0);
    assert(schedule.offered_count == 2u);
    // A launch that saw more versions than it remembers forgets the oldest first.
    for (unsigned i = 0; i < UPDATE_SCHEDULE_OFFERED; ++i)
    {
        char version[UPDATE_SCHEDULE_VERSION_BYTES];
        std::snprintf(version, sizeof(version), "01.003.%03u", i);
        assert(update_schedule_offer(&schedule, version) == 1);
    }
    assert(schedule.offered_count == UPDATE_SCHEDULE_OFFERED);
    assert(update_schedule_offer(&schedule, "01.003.007") == 0);
    assert(update_schedule_offer(&schedule, "01.002.005") == 1);
    // A new launch starts with nothing offered.
    update_schedule_init(&schedule);
    assert(update_schedule_offer(&schedule, "01.002.006") == 1);
    std::printf("offered once PASS\n");
}

} // namespace

int main()
{
    launch_checks_at_once_then_every_interval();
    a_failed_check_waits_as_long_as_an_answered_one();
    a_check_due_during_playback_runs_once_back_at_the_menu();
    each_version_is_offered_once_a_launch();
    std::printf("update schedule PASS\n");
    return 0;
}
