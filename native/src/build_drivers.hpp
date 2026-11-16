// SPDX-License-Identifier: MIT
// Builds the list of drivers when a session is ready: who they are, what the game had set, their form for the weekend.
#pragma once
#include <swofty/mixin.hpp>

#include <swofty/db.hpp>
#include <swofty/registry.hpp>

#include "ai_driver.hpp"
#include "form_roll.hpp"
#include "form_state.hpp"
#include "grid.hpp"
#include "ratings.hpp"
#include "results.hpp"

#include <swofty/live.hpp>

#include <chrono>
#include <cmath>
#include <ctime>
#include <map>
#include <string>
#include <vector>

namespace driver_form {

// The name the game shows for a driver. Most sessions have one for every car, so nothing slower is needed.
inline bool set_game_label(Driver &driver, const Entrant &entrant, const ai_driver::GameDrivers &game)
{
    std::string shown = game_name(game, entrant.car);
    if (shown.empty()) {
        return false;
    }
    driver.label = shown;
    return true;
}

// The name for a driver the game has no name for: their initials, otherwise the car number.
inline void set_label(Driver &driver, const Entrant &entrant, const std::map<int64_t, std::string> &initials_by_id)
{
    auto found = initials_by_id.find(entrant.driver);
    if (found != initials_by_id.end()) {
        driver.label = found->second;
    } else {
        driver.label = "#" + std::to_string(entrant.number);
    }
    if (!entrant.label.empty()) {
        db::Cursor cursor{};
        db::Value initials{};
        if (db::row("entrants.drivers", entrant.label, &cursor) == db::Result::ok) {
            if (db::cell(&cursor, "initials", &initials) == db::Result::ok && initials.s && *initials.s) {
                driver.label = initials.s;
            }
        }
    }
}

// The driver's id in the game's table, when the grid came from the event context.
inline void set_stat_id(Driver &driver, const Entrant &entrant)
{
    if (entrant.label.empty()) {
        return;
    }
    db::Cursor cursor{};
    db::Value id{};
    if (db::row("entrants.drivers", entrant.label, &cursor) == db::Result::ok &&
        db::cell(&cursor, "stat_id", &id) == db::Result::ok && id.i >= 0 && id.i < 0xf000) {
        driver.stat_id = uint16_t(id.i);
        driver.verified = true; // the database's own row for this driver
    }
}

// Milliseconds since a point in time, for the build log.
inline double milliseconds_since(Clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

// Gives every driver their form for the weekend: the book's when it is this weekend's, else a new roll from each
// driver's streak, regularity and the dice. A driver who joins mid weekend gets one without changing the others.
inline void give_weekend_forms()
{
    load_book(g_career);
    std::string key = weekend_key();
    long long now = (long long)std::time(nullptr);
    bool stale = g_career.empty() && now - g_book.weekend_time > kWeekendLife;
    std::vector<momentum::Entrant> entrants = book_entrants();
    if (key != g_book.weekend || stale) {
        g_book.weekend = key;
        g_book.weekend_time = now;
        momentum::roll_weekend(g_book, entrants, g_level, g_random);
        log("[driver-momentum] new weekend '%s' (%s, momentum %s, %zu record(s))", key.c_str(),
            g_career.empty() ? "Grand Prix" : "career", momentum::name_of(g_level), g_book.records.size());
        save_book("new weekend");
    } else {
        size_t before = g_book.forms.size();
        momentum::fill_weekend(g_book, entrants, g_level, g_random);
        if (g_book.forms.size() != before)
            save_book("a driver joined the weekend");
    }
    for (Driver &driver : g_drivers) {
        auto found = driver.verified ? g_book.forms.find(driver.stat_id) : g_book.forms.end();
        driver.form = found != g_book.forms.end() ? found->second : 0.0f;
    }
}

// The team of a car: the event context's (careers), else the badge the live state shows.
inline std::string team_of(uint32_t car)
{
    if (auto found = g_context_teams.find(car); found != g_context_teams.end() && !found->second.empty())
        return found->second;
    if (auto badge = live::team_badge(car))
        return *badge;
    return {};
}

// Reads the "Messages" setting for this kind of session.
inline std::string set_message_level(const ai_driver::GameDrivers &game)
{
    int kind = session_kind(game);
    if (kind >= 0) {
        g_session_kind = kind;
    }
    if (kind == 2) {
        g_race_session = true; // the game's own session type: a race or a sprint
    }
    const char *setting = "messages_practice";
    if (g_session_kind == 2) {
        setting = "messages_race";
    } else if (g_session_kind == 1) {
        setting = "messages_qualifying";
    }
    std::string level = option<std::string>(setting);
    g_message_level = 0;
    if (level == "low") {
        g_message_level = 1;
    } else if (level == "medium") {
        g_message_level = 2;
    } else if (level == "high") {
        g_message_level = 3;
    } else if (level == "ultra") {
        g_message_level = 4;
    }
    return level;
}

// Reads the grid and gives every driver their form. Returns false until the session is ready.
inline bool build()
{
    auto start = Clock::now();
    ai_driver::GameDrivers game;
    std::vector<Entrant> entrants;
    if (!ai_driver::find(game) || !read_entrants(game, entrants)) {
        return false;
    }
    for (const Entrant &entrant : entrants) {
        Driver driver;
        driver.car = entrant.car;
        driver.stat_id = entrant.driver;
        driver.verified = entrant.known && entrant.driver < 0xf000;
        if (entrant.car == 0xff) {
            if (g_build_failures == 0) {
                log("[driver-momentum] grid has no car numbers yet, waiting");
            }
            return false;
        }
        if (!ai_driver::read_stock(game, entrant.car, driver.stock) || driver.stock.characteristics[4] <= 0 ||
            driver.stock.base_lap_time <= 0) {
            if (g_build_failures < 3) {
                log("[driver-momentum] car %u not ready: speed_in %.3f base %.3f", unsigned(entrant.car),
                    double(driver.stock.characteristics[4]), double(driver.stock.base_lap_time));
            }
            return false;
        }
        driver.state = ai_driver::state_of(game, entrant.car);
        driver.master = ai_driver::master_of(game, entrant.car);
        driver.profile = ai_driver::profile_of(game, entrant.car);
        g_drivers.push_back(std::move(driver));
    }
    if (g_drivers.empty()) {
        return false;
    }
    auto cars_done = Clock::now();
    // The game's own names first. The table of initials is slow to walk, so it is only read for drivers without one.
    std::vector<size_t> unnamed;
    for (size_t index = 0; index < g_drivers.size(); index++) {
        Driver &driver = g_drivers[index];
        set_stat_id(driver, entrants[index]);
        if (!set_game_label(driver, entrants[index], game)) {
            unnamed.push_back(index);
        }
        driver.team = team_of(driver.car);
        if (driver.verified) {
            driver.ratings = ratings::of(driver.stat_id, entrants[index].label);
        }
        driver.regularity = momentum::regularity(driver.ratings);
    }
    // 0.1.0 fix: two cars with one id can't both be that driver: neither gets a form, a record or a result
    {
        std::map<uint16_t, int> seen;
        for (const Driver &driver : g_drivers)
            if (driver.verified)
                seen[driver.stat_id]++;
        for (Driver &driver : g_drivers)
            if (driver.verified && seen[driver.stat_id] > 1) {
                driver.verified = false;
                log("[driver-momentum] car %u: stat %u is on %d cars; this car gets no form", driver.car,
                    unsigned(driver.stat_id), seen[driver.stat_id]);
            }
    }
    if (!unnamed.empty()) {
        const auto &initials_by_id = driver_initials();
        for (size_t index : unnamed) {
            set_label(g_drivers[index], entrants[index], initials_by_id);
        }
    }
    auto names_done = Clock::now();
    g_records = game.records;
    give_weekend_forms();
    std::string level = set_message_level(game);
    log("[driver-momentum] %zu drivers, %.1f s a lap at full form, session kind %d, messages %s", g_drivers.size(),
        double(g_strength.seconds), g_session_kind, level.c_str());
    for (const Driver &driver : g_drivers) {
        log("[driver-momentum] car %u %s (stat %u%s, team '%s'): form %+.2f, regularity %.2f", driver.car,
            driver.label.c_str(), unsigned(driver.stat_id), driver.verified ? "" : ", unknown", driver.team.c_str(),
            double(driver.form), double(driver.regularity));
    }
    log("[driver-momentum] grid built in %.1f ms: cars %.1f, names %.1f (%zu without a game name), weekend %.1f",
        milliseconds_since(start), std::chrono::duration<double, std::milli>(cars_done - start).count(),
        std::chrono::duration<double, std::milli>(names_done - cars_done).count(), unnamed.size(),
        milliseconds_since(names_done));
    return true;
}

} // namespace driver_form
