// SPDX-License-Identifier: MIT
// Turns a driver's form into changes in the game: their pace, their driving quality and how hard they defend.
#pragma once
#include <swofty/mixin.hpp>

#include <swofty/systems/cars.hpp>

#include "ai_driver.hpp"
#include "form_state.hpp"

#include <cmath>

namespace driver_form {

using cars::live::Cars;

constexpr const char *kBlocking = "ai.blocking.max_blocking_distance"; // how far a driver defends

// Out-of-form drivers lose time through their driving as well, so the dial is smaller on that side.
inline float shift_of(const Driver &driver)
{
    float shift = 0;
    if (driver.form < 0) {
        shift = -driver.form * g_strength.seconds * 0.6f;
    } else {
        shift = -driver.form * g_strength.seconds;
    }
    if (driver.patch == Patch::bad) {
        shift += 1.0f * g_strength.patch;
    }
    if (driver.patch == Patch::hot) {
        shift -= 0.4f * g_strength.patch;
    }
    return shift;
}

inline float quality_of(const Driver &driver)
{
    float quality = 1.0f;
    if (driver.form < 0) {
        quality = 1.0f + driver.form * g_strength.depth;
    }
    if (driver.patch == Patch::bad) {
        quality = quality * 0.6f;
    }
    return quality;
}

// Gives the driver back to the game as it was: only what this addon wrote (a car it never wrote to, yours above all,
// is never touched).
inline void clear_driver(Driver &driver)
{
    for (auto [state, wrote] : {std::pair{driver.state, &driver.wrote_state}, std::pair{driver.master, &driver.wrote_master}}) {
        if (!state || !*wrote) {
            continue;
        }
        for (int characteristic = 0; characteristic < 6; characteristic++) {
            ai_driver::clear_characteristic(state, characteristic);
        }
        ai_driver::clear_difficulty(state);
        *wrote = false;
    }
    if (driver.profile && driver.wrote_profile) {
        ai_driver::poke<float>(driver.profile + 0x148, driver.stock.base_lap_time);
        driver.wrote_profile = false;
    }
}

// Whether this addon has anything of its own in the game for the driver.
inline bool wrote_any(const Driver &driver)
{
    return driver.wrote_profile || driver.wrote_state || driver.wrote_master || !driver.layers.empty();
}

inline void write_driver(Driver &driver)
{
    if (driver.human)
        return; // never yours
    constexpr int kWrittenCharacteristics[] = {0, 1, 4, 5}; // aggression, control, speed in, speed out
    float quality = quality_of(driver);
    for (auto [state, wrote] : {std::pair{driver.state, &driver.wrote_state}, std::pair{driver.master, &driver.wrote_master}}) {
        if (!state) {
            continue;
        }
        *wrote = true;
        // A moment: for a few seconds the driver drives far below their level.
        if (driver.patch == Patch::moment) {
            ai_driver::set_difficulty(state, driver.stock.difficulty * 0.35f);
        } else {
            ai_driver::clear_difficulty(state);
        }
        for (int characteristic : kWrittenCharacteristics) {
            if (quality < 0.999f) {
                float stock_value = driver.stock.characteristics[characteristic];
                ai_driver::set_characteristic(state, characteristic, stock_value * quality);
            } else {
                ai_driver::clear_characteristic(state, characteristic);
            }
        }
    }
    if (driver.profile) {
        ai_driver::poke<float>(driver.profile + 0x148, driver.stock.base_lap_time + shift_of(driver));
        driver.wrote_profile = true;
    }
}

// Good form makes a driver defend harder, bad form makes them give way sooner.
inline void start_layers(Driver &driver)
{
    if (driver.human)
        return;
    Layer layer =
        Layer::scale(Cars::car(cars::live::Car(driver.car)), kBlocking, std::exp2(driver.form * g_strength.blocking));
    if (layer.ok()) {
        driver.layers.push_back(std::move(layer));
    }
}

} // namespace driver_form
