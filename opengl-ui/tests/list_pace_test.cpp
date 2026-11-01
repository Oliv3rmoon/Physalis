// ProsperoTV - How fast a held direction, or a held L2 or R2, goes through a long list.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#include "tv/list_pace.hpp"
#include <gtest/gtest.h>
#include <algorithm>
#include <string>
#include <vector>

namespace
{
hui::InputFrame step(hui::Direction direction, bool repeat)
{
    hui::InputFrame input;
    input.connected = true;
    input.nav = direction;
    input.nav_repeat = repeat;
    return input;
}

hui::InputFrame trigger(hui::Action action, bool press)
{
    hui::InputFrame input;
    input.connected = true;
    input.held = hui::action_bit(action);
    if (press)
        input.pressed = input.held;
    return input;
}

TEST(ListPace, AHeldDirectionTakesMoreRowsTheLongerItIsHeld)
{
    ptv::HeldSteps steps;
    EXPECT_EQ(steps.rows(step(hui::Direction::down, false)), 1);
    std::vector<int> rows;
    for (int i = 0; i < 32; ++i)
        rows.push_back(steps.rows(step(hui::Direction::down, true)));
    // The kit repeats every 0.11 s after 0.35 s: one row a repeat for about a second, then two,
    // four and eight.
    for (int i = 0; i < 32; ++i)
        EXPECT_EQ(rows[static_cast<std::size_t>(i)], i < 7 ? 1 : i < 15 ? 2 : i < 27 ? 4 : 8) << i;
    // A press of its own, or another direction, starts again at one.
    EXPECT_EQ(steps.rows(step(hui::Direction::up, false)), 1);
    EXPECT_EQ(steps.rows(step(hui::Direction::up, true)), 1);
}

TEST(ListPace, AHeldTriggerTurnsPagesFasterTheLongerItIsHeld)
{
    ptv::PageTurns pages;
    bool repeat = true;
    EXPECT_EQ(pages.turn(trigger(hui::Action::jump_next, true), 10.0, &repeat), 1);
    EXPECT_FALSE(repeat);
    EXPECT_TRUE(pages.holding());
    // Held: nothing more until 0.4 s, then a page every 0.12 s.
    EXPECT_EQ(pages.turn(trigger(hui::Action::jump_next, false), 10.39), 0);
    EXPECT_EQ(pages.turn(trigger(hui::Action::jump_next, false), 10.41, &repeat), 1);
    EXPECT_TRUE(repeat);
    EXPECT_EQ(pages.turn(trigger(hui::Action::jump_next, false), 10.47), 0);
    EXPECT_EQ(pages.turn(trigger(hui::Action::jump_next, false), 10.53), 1);
    // Held for many seconds at 60 frames a second: two, four, then eight pages a turn.
    int most = 0, turned = 0;
    for (double now = 10.54; now < 18.0; now += 1.0 / 60.0)
    {
        const int pages_now = pages.turn(trigger(hui::Action::jump_next, false), now);
        if (now < 13.0)
            EXPECT_LE(pages_now, 1) << now;
        most = std::max(most, pages_now);
        turned += pages_now;
    }
    EXPECT_EQ(most, 8);
    EXPECT_GT(turned, 250); // a steady page every 0.07 s would be a hundred
    // Let go: no more pages, and the next press is one page again.
    EXPECT_EQ(pages.turn(hui::InputFrame{}, 18.1), 0);
    EXPECT_FALSE(pages.holding());
    EXPECT_EQ(pages.turn(trigger(hui::Action::jump_prev, true), 20.0), -1);
    EXPECT_EQ(pages.turn(trigger(hui::Action::jump_prev, false), 20.41), -1);
    // The other trigger held instead ends the hold.
    EXPECT_EQ(pages.turn(trigger(hui::Action::jump_next, false), 20.60), 0);
    EXPECT_FALSE(pages.holding());
    EXPECT_EQ(pages.turn(trigger(hui::Action::jump_next, true), 21.0), 1);
    pages.stop();
    EXPECT_EQ(pages.turn(trigger(hui::Action::jump_next, false), 22.0), 0);
}

TEST(ListPace, AListGoesFasterAndStopsAtItsEnds)
{
    hui::ui::ListView list;
    std::vector<hui::ui::ListItem> items(50);
    for (std::size_t i = 0; i < items.size(); ++i)
        items[i].title = "Row " + std::to_string(i);
    list.set_items(std::move(items));
    list.set_focus(0, true);
    hui::ui::Feedback feedback;
    ptv::HeldSteps steps;
    ptv::PageTurns pages;
    const auto paced = [&](const hui::InputFrame &input, double now = 0.0)
    { return ptv::pace_list(list, input, steps, &pages, now, 8, feedback); };
    // A press and the first repeats are the list's own single steps.
    EXPECT_FALSE(paced(step(hui::Direction::down, false)));
    for (int i = 0; i < 7; ++i)
        EXPECT_FALSE(paced(step(hui::Direction::down, true)));
    EXPECT_EQ(list.focus(), 0); // nothing moved here: the list itself takes those
    EXPECT_TRUE(paced(step(hui::Direction::down, true)));
    EXPECT_EQ(list.focus(), 2);
    // L2 and R2: a page of eight, and at the end the hold stops.
    EXPECT_TRUE(paced(trigger(hui::Action::jump_next, true), 1.0));
    EXPECT_EQ(list.focus(), 10);
    for (int i = 0; i < 400; ++i)
        (void)paced(trigger(hui::Action::jump_next, false), 1.0 + i / 60.0);
    EXPECT_EQ(list.focus(), 49);
    EXPECT_FALSE(pages.holding());
    EXPECT_TRUE(paced(trigger(hui::Action::jump_next, true), 9.0)); // refused, and consumed
    EXPECT_EQ(list.focus(), 49);
    EXPECT_TRUE(paced(trigger(hui::Action::jump_prev, true), 10.0));
    EXPECT_EQ(list.focus(), 41);
    // Without pages (Sources, where L2 and R2 do other things) the triggers are left alone.
    EXPECT_FALSE(ptv::pace_list(list, trigger(hui::Action::jump_prev, true), steps, nullptr, 0.0, 0,
                                feedback));
    EXPECT_EQ(list.focus(), 41);
}
} // namespace
