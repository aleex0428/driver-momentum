// SPDX-License-Identifier: MIT
// Whose car it is. Your car is protected when anything says it is yours: the event context (careers), the game handing
// it to you (PlayerControl), the controls read every 10 s, or the live state's player. A car is ours (the form is
// applied) only when the game has said the computer drives it and nothing says it is yours: a control that can't be
// read never takes the protection away. Once yours, always yours for the session.
#pragma once
#include <swofty/mixin.hpp>

#include <swofty/live.hpp>

#include "form_state.hpp"

namespace driver_form {

// No car's control could be read three times running: a core without the control reads. Then, and only when the event
// context said which car is yours, every other car is the computer's.
inline bool g_control_blind = false;
inline int g_control_misses = 0;

// the live state's player car (also while the spectator HUD is up)
inline bool live_player(uint32_t car)
{
    auto s = live::session();
    return s && (s->have & SWOFTY_LIVE_SHAVE_PLAYER) && s->player >= 0 && uint32_t(s->player) == car;
}

inline bool is_protected(const Driver &driver)
{
    return g_context_humans.count(driver.car) != 0 || g_player_cars.count(driver.car) != 0 || live_player(driver.car);
}

// the live state says the computer drives the car and it isn't the player's
inline bool live_says_ai(uint32_t car)
{
    auto c = live::car(car);
    return c && live::ai_driving(*c) && !live::is_player(*c);
}

// the next tick's owner: true when the form is applied
inline bool ours(const Driver &driver)
{
    if (is_protected(driver))
        return false;
    return driver.control_ai || live_says_ai(driver.car) || (g_control_blind && !g_context_humans.empty());
}

} // namespace driver_form
