// SPDX-License-Identifier: MIT
// Turns a driver's form into changes in the game: their pace, their driving quality and how hard they defend.
// 0.2.0: only through the launcher's official live car levers (cars::live layers, the Addon Browser's rule: launcher
// bindings and APIs only). The addon writes nothing into the game's memory: the core keeps each layer, applies it
// again after every car rebuild and reset, and destroying the layer gives the car the game's own value back.
#pragma once
#include <swofty/mixin.hpp>

#include <swofty/systems/cars.hpp>

#include "form_state.hpp"

#include <algorithm>
#include <cmath>

namespace driver_form {

using cars::live::Cars;

constexpr const char *kBlocking = "ai.blocking.max_blocking_distance"; // how far a driver defends
constexpr const char *kGrip = "ai.grip_scale";                         // the AI's grip input (dry weather only)
constexpr const char *kPower = "engine.power_scale";                   // the engine's drive torque (any weather)
constexpr const char *kOffset = "ai.offset_variation.max_offset";      // how far the car wanders off its line
constexpr const char *kAttack = "ai.overtake.attacker_close_enough_car_lengths"; // how close before it attacks

// The pace levers' size: a lap this many seconds long is the reference, and each lever carries half of the change
// (the grip only works in the dry, so in the wet a form shows at about half its size). A lever's change per relative
// change of the lap: grip and power both turn about 3 % into 1 % of a lap. To be calibrated in the game with
// post_game.py (the average lap of each AI car against its form).
constexpr float kReferenceLap = 90.0f;
constexpr float kGripPerLap = 3.0f;
constexpr float kPowerPerLap = 3.0f;
// the levers' bounds, well inside the core's ranges (grip and power 0..4)
constexpr float kPaceMin = 0.85f, kPaceMax = 1.15f;

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

// What the levers should be for a driver now. Pure, for the tests.
inline Levers levers_of(const Driver &driver)
{
    Levers l;
    // the pace: a shift of s seconds a lap is -s / kReferenceLap of the lap, split between grip and power
    float rel = -shift_of(driver) / kReferenceLap;
    l.grip = 1.0f + 0.5f * kGripPerLap * rel;
    l.power = 1.0f + 0.5f * kPowerPerLap * rel;
    // a lower driving quality: a wider, less tidy line, a little less grip in the corners, and less eager to attack
    float quality = std::clamp(quality_of(driver), 0.2f, 1.0f);
    l.offset = 1.0f / quality;
    l.grip *= 1.0f - 0.02f * (1.0f - quality);
    l.attack = quality;
    // A moment: for a few seconds the driver drives far below their level.
    if (driver.patch == Patch::moment) {
        l.grip *= 0.93f;
        l.power *= 0.97f;
        l.offset *= 2.0f;
    }
    l.grip = std::clamp(l.grip, kPaceMin, kPaceMax);
    l.power = std::clamp(l.power, kPaceMin, kPaceMax);
    l.offset = std::clamp(l.offset, 1.0f, 4.0f);
    l.attack = std::clamp(l.attack, 0.3f, 1.0f);
    return l;
}

inline bool same_levers(const Levers &a, const Levers &b)
{
    auto close = [](float x, float y) { return std::fabs(x - y) < 0.002f; };
    return close(a.grip, b.grip) && close(a.power, b.power) && close(a.offset, b.offset) && close(a.attack, b.attack);
}

// Gives the driver back to the game as it was: the layers go, and the core puts the game's values back. A car the
// addon never layered (yours above all) is never touched.
inline void clear_driver(Driver &driver)
{
    driver.lever_layers.clear();
    driver.levers_on = false;
}

// Whether this addon has anything of its own in the game for the driver.
inline bool wrote_any(const Driver &driver) { return !driver.lever_layers.empty() || !driver.layers.empty(); }

// the layers a set of levers needs (a lever at 1 needs none)
inline void layer_levers(Driver &driver, const Levers &l)
{
    driver.lever_layers.clear();
    Cars car = Cars::car(cars::live::Car(driver.car));
    auto add = [&](const char *path, float factor) {
        if (std::fabs(factor - 1.0f) < 1e-4f)
            return;
        Layer layer = Layer::scale(car, path, factor);
        if (layer.ok())
            driver.lever_layers.push_back(std::move(layer));
        else if (!driver.lever_refused) {
            driver.lever_refused = true;
            log("[driver-momentum] car %u: the core refused the %s layer (%d)", driver.car, path, int(layer.result()));
        }
    };
    add(kGrip, l.grip);
    add(kPower, l.power);
    add(kOffset, l.offset);
    add(kAttack, l.attack);
}

// Keeps an AI driver's levers at what their form says; the layers are made again only when a value moves.
inline void write_driver(Driver &driver)
{
    if (driver.human || g_leave_ai)
        return; // never yours; and never with Driver Form on too
    Levers want = levers_of(driver);
    if (driver.levers_on && same_levers(want, driver.applied))
        return;
    layer_levers(driver, want);
    driver.applied = want;
    driver.levers_on = true;
}

// Good form makes a driver defend harder, bad form makes them give way sooner.
inline void start_layers(Driver &driver)
{
    if (driver.human || g_leave_ai)
        return;
    Layer layer =
        Layer::scale(Cars::car(cars::live::Car(driver.car)), kBlocking, std::exp2(driver.form * g_strength.blocking));
    if (layer.ok()) {
        driver.layers.push_back(std::move(layer));
    }
}

} // namespace driver_form
