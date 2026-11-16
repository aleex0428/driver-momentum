// SPDX-License-Identifier: MIT
// What the addon remembers while it runs: each driver's form and the settings that are shared by every file.
#pragma once
#include <swofty/mixin.hpp>

#include <swofty/registry.hpp>
#include <swofty/systems/cars.hpp>

#include "ai_driver.hpp"
#include "momentum.hpp"

#include <array>
#include <chrono>
#include <map>
#include <mutex>
#include <random>
#include <set>
#include <string>
#include <vector>

namespace driver_form {

using namespace swofty;
using cars::live::Layer;
using Clock = std::chrono::steady_clock;

// How strong the form is, from the "Form strength" setting.
struct Strength {
    float seconds;  // pace swing at form +-1, seconds a lap
    float depth;    // how far driving quality falls at form -1
    float patch;    // size of bad patches and hot spells
    float blocking; // defending distance = stock x 2^(form x this)
};

inline bool strength_of(const std::string &name, Strength &strength)
{
    if (name == "subtle") {
        strength = {0.8f, 0.3f, 0.7f, 0.5f};
    } else if (name == "normal") {
        strength = {1.5f, 0.45f, 1.0f, 1.0f};
    } else if (name == "wild") {
        strength = {2.2f, 0.6f, 1.5f, 1.6f};
    } else if (name == "extreme") {
        strength = {3.5f, 0.7f, 2.5f, 2.5f};
    } else {
        return false;
    }
    return true;
}

enum class Patch { none, bad, hot, moment };

struct Driver {
    uint32_t car = 0;
    uint16_t stat_id = 0;
    bool verified = false;  // the stat id is this driver's own (read from the game or its database). 0 is Carlos Sainz
                            // when verified; an unverified driver gets no form, no record and no result
    std::string label;
    std::string team;       // tells teammates apart for the streak; empty when unknown
    momentum::Ratings ratings; // experience and awareness from the game database; unknown below 0
    float regularity = 1;      // how far this driver's form swings: momentum::regularity(ratings)
    uintptr_t state = 0;   // the game's values for this car, found once so the tick stays cheap
    uintptr_t master = 0;
    uintptr_t profile = 0;
    float form = 0;        // -1 bad .. +1 good
    ai_driver::Stock stock;
    std::vector<Layer> layers;
    Patch patch = Patch::none;
    Clock::time_point patch_until;
    Clock::time_point next_event;
    bool human = true;     // the first tick decides
    bool control_ai = false; // the game said the computer drives this car (its control, or the live state)
    // what this addon changed in the game for this car: only that is given back
    bool wrote_profile = false, wrote_state = false, wrote_master = false;
    int crossings = 0;
    Clock::time_point last_crossing{};
    int last_position = 0;
    int pit_crossing = -1;
};

inline std::vector<Driver> g_drivers;
inline uintptr_t g_records = 0; // the record table the cached pointers belong to
inline Clock::time_point g_last_system_check;
inline Clock::time_point g_last_control_check;
inline bool g_check_control = true; // read who drives every car on the next tick
inline size_t g_control_next = 0;    // the next car the control check reads, a car a tick
inline int g_control_pass_read = 0; // cars whose control could be read in this pass
inline bool g_time_trial = false;   // a Time Trial is left alone: its cars are your own ghosts
// 0.1.0 fix: a mode the addon doesn't know (split screen, story, F1 World...) is left alone too; only a career and a
// Grand Prix get forms
inline bool g_other_mode = false;
inline int g_session_type = -1; // the event context's session type (events::SessionType), -1 unknown
// Driver Momentum: the loaded career's book (or the Grand Prix weekend's), what it is called, and how much the
// streak counts. A career's book is careers/<id>.txt in the addon's storage; a Grand Prix's is weekend.txt.
inline momentum::Book g_book;
inline std::string g_book_file;                // the storage file g_book came from; "" none loaded
inline std::string g_career;                   // the career id; "" in a Grand Prix
inline std::string g_weekend_hint;             // the weekend's key from the session start; "" unknown
inline momentum::Level g_level = momentum::Level::normal;
inline std::map<uint32_t, std::string> g_context_teams; // car -> team, from the event context (careers)
inline std::set<uint32_t> g_context_humans;            // cars the event context says are yours
inline bool g_race_session = false;            // this session is a race (race.start, or the game's session type)

// 0.1.0 fix: the weekend's first race is kept in the book itself (momentum::Book::first), so closing the game between
// a sprint and its race no longer loses the race.
inline std::mt19937 g_random{std::random_device{}()};
inline Strength g_strength{};
inline bool g_patches_on = true;
inline bool g_armed = false;
inline bool g_board_done = false;
inline Clock::time_point g_last_tick;
inline Clock::time_point g_last_build_try;
inline int g_build_failures = 0; // after a few, the grid is only tried every 10 s
inline Clock::time_point g_last_toast;
inline Clock::time_point g_race_started;
inline bool g_race_running = false;
inline bool g_paused = false;
inline Clock::time_point g_paused_since;
inline std::mutex g_mutex;
inline std::set<uint32_t> g_player_cars;          // cars ever seen under human control, never touched again
inline std::array<int, 128> g_cars_at_crossing{}; // how many cars have reached each line crossing
inline int g_session_kind = 2;                    // 0 practice, 1 qualifying, 2 race; each has its own Messages setting
inline int g_message_level = 0;                   // 0 none, 1 low, 2 medium, 3 high, 4 ultra

inline float random_between(float low, float high)
{
    return std::uniform_real_distribution<float>(low, high)(g_random);
}

} // namespace driver_form
