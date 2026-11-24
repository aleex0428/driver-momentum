// SPDX-License-Identifier: MIT
// Finds out who is on the grid: which cars, which drivers, what the game calls them, and what kind of session it is.
// 0.2.0: everything from the launcher's own APIs: the event context (careers), the live state's cars and their
// identity (the driver's stat id and race number, from the game's UDP participants), live::driver_name and the
// session type. Nothing is read from the game's memory by the addon itself.
#pragma once
#include <swofty/mixin.hpp>

#include <swofty/db.hpp>
#include <swofty/live.hpp>
#include <swofty/registry.hpp>

#include <cctype>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace driver_form {

using namespace swofty;

struct Entrant {
    uint16_t driver = 0; // matches entrants.drivers stat_id
    bool known = false;  // 0.1.0 fix: the stat id was read (0 is Carlos Sainz then); a failed read leaves it unknown
    uint8_t number = 0;
    uint8_t car = 0xff;  // the game's vehicle id
    std::string label;   // the DB row, when the grid came from the event context
};

// The name the game shows for a car, in capitals. Empty when it has none.
inline std::string game_name(uint32_t car)
{
    auto shown = live::driver_name(car);
    if (!shown || shown->empty()) {
        return {};
    }
    std::string name = *shown;
    for (char &character : name) {
        if (static_cast<unsigned char>(character) < 0x80) {
            character = char(std::toupper((unsigned char)character));
        }
    }
    return name;
}

inline int g_game_session_type = -1; // the game's own session type number, for the log; -1 unknown

// The kind of session: 0 practice, 1 qualifying, 2 race. -1 when it can't be read.
// The live state's session type is the game's own number (1-4 practice, 5-14 qualifying and the shootout, 15-17 race).
inline int session_kind()
{
    auto session = live::session();
    if (!session || !live::has(*session, SWOFTY_LIVE_SHAVE_TYPE)) {
        return -1;
    }
    uint32_t type = session->type;
    if (int(type) != g_game_session_type) {
        log("[driver-momentum] game session type %u", type);
    }
    g_game_session_type = int(type);
    if (type >= 1 && type <= 4) {
        return 0;
    }
    if (type >= 5 && type <= 14) {
        return 1;
    }
    if (type >= 15 && type <= 17) {
        return 2;
    }
    return -1;
}

inline std::vector<Entrant> g_context_entrants; // from the event context; careers fill it, a stock Grand Prix does not

// Says once which way the grid was found.
inline void report_route(int route, size_t count)
{
    static int last_route = -1;
    if (route == last_route) {
        return;
    }
    last_route = route;
    const char *const kRoutes[] = {"career context", "live state, with each driver's identity",
                                   "live state, without the drivers' identity (no forms)"};
    log("[driver-momentum] grid from %s: %zu cars", kRoutes[route], count);
}

// The live state's session number: a new one means the grid is to be built again. 0 when there is none.
inline uint32_t live_generation()
{
    auto session = live::session();
    return session ? session->generation : 0;
}

// The grid. A career's event context names every seat. Elsewhere the live state lists the racing cars, and its
// identity (the game's UDP participants) says which driver is in each; a car without one is listed unknown and gets no
// form. With `allow_unknown` false the grid waits for at least one identity.
inline bool read_entrants(std::vector<Entrant> &out, bool allow_unknown)
{
    out.clear();
    if (!g_context_entrants.empty()) {
        out = g_context_entrants;
        report_route(0, out.size());
        return true;
    }
    if (!live::available()) {
        return false;
    }
    auto session = live::session();
    if (!session || !live::has(*session, SWOFTY_LIVE_SHAVE_TYPE) || session->type < 1) {
        return false;
    }
    int identified = 0;
    for (uint32_t car = 0; car < SWOFTY_LIVE_MAX_CARS; car++) {
        auto live_car = live::car(car);
        if (!live_car || !live::is_racer(*live_car)) {
            continue;
        }
        Entrant entrant;
        entrant.car = uint8_t(car);
        entrant.number = uint8_t(car);
        entrant.driver = uint16_t(0xf000 + car);
        if (auto detail = live::car_detail(car); detail && live::has(*detail, SWOFTY_LIVE_HAVE_IDENTITY)) {
            entrant.driver = detail->driver_id;
            entrant.known = detail->driver_id < 0xf000;
            entrant.number = detail->race_number;
            identified += entrant.known ? 1 : 0;
        }
        out.push_back(entrant);
    }
    if (out.empty() || (!identified && !allow_unknown)) {
        return false;
    }
    report_route(identified ? 1 : 2, out.size());
    return true;
}

// stat_id -> initials from the game DB table entrants.drivers
// The table does not change while the game runs, so it is read once and kept.
inline const std::map<int64_t, std::string> &driver_initials()
{
    static std::map<int64_t, std::string> initials_by_id;
    if (!initials_by_id.empty() || !db::available()) {
        return initials_by_id;
    }
    db::Cursor cursor{};
    if (db::open("entrants.drivers", &cursor) != db::Result::ok) {
        return initials_by_id;
    }
    for (int guard = 0; guard < 4000; guard++) {
        db::Value id{};
        db::Value initials{};
        bool has_id = db::cell(&cursor, "stat_id", &id) == db::Result::ok;
        bool has_initials = db::cell(&cursor, "initials", &initials) == db::Result::ok;
        if (has_id && has_initials && initials.s && *initials.s) {
            initials_by_id[id.i] = initials.s;
        }
        if (db::step(&cursor) != db::Result::ok) {
            break;
        }
    }
    return initials_by_id;
}

} // namespace driver_form
