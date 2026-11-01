// ProsperoTV - How fast a held direction, or a held L2 or R2, goes through a long list.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "tv/shared.hpp"
#include "ui/components/list.hpp"

#include <algorithm>

namespace ptv
{

// Up and Down held: the kit repeats a held direction every 0.11 s after 0.35 s
// (hui::InputSettings). From the 8th repeat (a little over a second into the hold) each
// repeat goes two rows, from the 16th four, and from the 28th (past three seconds) eight.
// A press, or a change of direction, starts again at one.
class HeldSteps
{
  public:
    // How many rows this frame's Up or Down goes; 1 for a press.
    int rows(const InputFrame &input)
    {
        streak_ = input.nav_repeat ? streak_ + 1 : 0;
        return streak_ >= 28 ? 8 : streak_ >= 16 ? 4 : streak_ >= 8 ? 2 : 1;
    }

  private:
    int streak_ = 0;
};

// L2 and R2 turn a page; held, they keep turning: after 0.4 s every 0.12 s, after 1.6 s
// every 0.07 s, then two, four and eight pages a turn after 3, 5 and 7 s (a provider's
// "all movies" is tens of thousands of rows). The caller says what time it is.
class PageTurns
{
  public:
    // Pages to turn this frame, negative back; `repeat` says it is a turn of a hold.
    int turn(const InputFrame &input, double now, bool *repeat = nullptr)
    {
        if (repeat != nullptr)
            *repeat = false;
        const int pressed = input.is_pressed(Action::jump_next)   ? 1
                            : input.is_pressed(Action::jump_prev) ? -1
                                                                  : 0;
        if (pressed != 0)
        {
            direction_ = pressed;
            started_ = now;
            due_ = 0.40;
            return pressed;
        }
        const int held = input.is_held(Action::jump_next)   ? 1
                         : input.is_held(Action::jump_prev) ? -1
                                                            : 0;
        if (held == 0 || held != direction_)
        {
            direction_ = 0;
            return 0;
        }
        const double hold = now - started_;
        if (hold < due_)
            return 0;
        due_ += hold >= 1.6 ? 0.07 : 0.12;
        if (repeat != nullptr)
            *repeat = true;
        return held * (hold >= 7.0 ? 8 : hold >= 5.0 ? 4 : hold >= 3.0 ? 2 : 1);
    }
    // The hold ends here: the end of the list, or something else took the controller.
    void stop()
    {
        direction_ = 0;
    }
    bool holding() const
    {
        return direction_ != 0;
    }

  private:
    int direction_ = 0;
    double started_ = 0.0, due_ = 0.0;
};

// A list that goes faster: a held Up or Down several rows at a time and, given `pages`, L2
// and R2 a page of `page` rows. True when it moved the focus (or refused a page at the end);
// false leaves the input to the list, which takes single steps and refuses at its ends.
inline bool pace_list(ui::ListView &list, const InputFrame &input, HeldSteps &steps,
                      PageTurns *pages, double now, int page, ui::Feedback &feedback)
{
    const int count = static_cast<int>(list.items().size());
    if (count == 0)
        return false;
    const int from = list.focus();
    if (input.nav == Direction::up || input.nav == Direction::down)
    {
        const int rows = steps.rows(input);
        const int to =
            std::clamp(from + (input.nav == Direction::down ? rows : -rows), 0, count - 1);
        if (rows < 2 || to == from || list.items()[static_cast<std::size_t>(to)].header)
            return false;
        list.set_focus(to, false);
        feedback.play(audio::Cue::focus);
        return true;
    }
    bool repeat = false;
    const int turn = pages != nullptr ? pages->turn(input, now, &repeat) : 0;
    if (turn == 0)
        return false;
    const int to = std::clamp(from + turn * page, 0, count - 1);
    if (to == from || list.items()[static_cast<std::size_t>(to)].header)
    {
        pages->stop(); // the end of the list ends the hold
        if (!repeat)
            feedback.play(audio::Cue::error, 1.0f, 0.0f, 0.6f);
        return true;
    }
    // A press glides to the next page; a hold flips them, as on Live TV.
    list.set_focus(to, repeat);
    feedback.play(audio::Cue::tab, turn > 0 ? 0.96f : 1.04f, 0.0f, repeat ? 0.5f : 1.0f);
    return true;
}

} // namespace ptv
