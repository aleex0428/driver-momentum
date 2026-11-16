// SPDX-License-Identifier: MIT
//
// The Driver Momentum form service (alexs.driver-momentum), for other addons to include. Copy this header (it is
// MIT) and look the service up with
//
//     swofty::services::when<DriverMomentumForm>([](const DriverMomentumForm &f) { g_form = &f; });
//
// The callback runs only when Driver Momentum is installed and on in the profile. Stat ids are entrants.drivers stat
// ids. Call it from any thread; it answers from what the addon last read (the grid of the running session and the
// loaded career's book).
#pragma once
#include <cstdint>

struct DriverMomentumForm {
    static constexpr const char *name = "driver_momentum.form";
    static constexpr uint32_t version = 1;

    uint32_t size;
    // A driver's form for this weekend (-1 badly out of form .. +1 flying) and their streak in the loaded career
    // (-1 a slump .. +1 a hot streak; 0 in a Grand Prix). Either pointer may be null.
    // Returns 0: both known; 1: the driver has no form this weekend (form 0; the streak is still given when the
    // career has a record for them); 2: no weekend yet (nothing written).
    int (*form)(uint32_t driver_stat, float *form, float *streak);
    // The races the loaded career's book has counted; 0 in a Grand Prix or before the first race.
    int (*races)(void);
    // Append new calls here and keep size first.
};
