// SPDX-License-Identifier: MIT
// A career race's result, for the streaks. While a race runs, the classification is kept as last seen: from the
// core's live state (position and result of every car), else from the line crossings the addon already counts. When
// the session ends and the race ran to the flag, the book counts it.
//
// 0.1.0 fixes:
// - Only a settled classification counts: no two cars on one position, the positions in a row, and every car at the
//   flag, retired, disqualified or not classified (the "final" reading); or, at the session's end, most of the field
//   at the flag with the rest behind them in order (the "settled" one). A reading with one car at the flag, five
//   still running and two in P2 counted before.
// - Retired, disqualified and not classified are told apart; a lapped car still crossing the line is no retirement.
// - Every race has an identity (its weekend, its session type when known and its result): one is never counted twice.
// - Sprints: the game does not say reliably which race is the sprint, so the second race of a weekend tells: when it
//   comes, the first was the sprint. The first race now waits in the book (not only in memory), so a reload between
//   the sprint and the race no longer loses the race. A race run again (a restart, the same session type twice)
//   replaces the first instead of making it a sprint, and a session that ended in a restart counts nothing.
#pragma once
#include <swofty/mixin.hpp>

#include <swofty/events.hpp>
#include <swofty/live.hpp>

#include "form_roll.hpp"
#include "form_state.hpp"
#include "momentum.hpp"

#include <algorithm>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace driver_form {

inline std::vector<momentum::Result> g_last_results;  // the classification as last seen in this session
inline std::vector<momentum::Result> g_final_results; // the last reading that was final (see above)
inline bool g_final = false;
inline bool g_flag_seen = false;    // a car has taken the chequered flag: the race ran to the end
inline const char *g_results_from = "nothing";
inline std::string g_unsettled;     // why the last reading wasn't final, for the log
inline int g_session_laps = 0;      // the race's length from the event context; 0 unknown
inline int g_leader_crossings = 0;  // the most line crossings any car made
inline int g_live_type = -1;        // the live state's session type in this race (1..18); -1 unknown
inline int g_at_flag = 0, g_in_race = 0; // the last reading: cars at the flag, cars in the race (every car, known or not)
inline int g_session_serial = 0;    // counts the sessions of this game run: a race run again in one session
inline bool g_last_in_row = false;  // the last live reading's positions (every car) were 1..n

// 0.1.0 fix: a driver whose identity was read (Carlos Sainz's stat id is 0); an unknown car never counts
inline bool countable(const Driver &driver) { return driver.verified && driver.stat_id < 0xf000; }

// the classified positions are 1..n with none missing and none twice
inline bool in_a_row(const std::vector<momentum::Result> &results)
{
    std::vector<int> positions;
    for (const momentum::Result &r : results)
        if (r.finished)
            positions.push_back(r.position);
    std::sort(positions.begin(), positions.end());
    for (size_t i = 0; i < positions.size(); i++)
        if (positions[i] != int(i) + 1)
            return false;
    return true;
}

// The classification from the live state: false when the core has none this frame (it is rejected whole while the
// game re-sorts its order, and so is a reading with two cars on one position). The order is checked over every car in
// the race, a car without an identity or yours included; only the identified ones are kept.
inline bool read_live_results()
{
    if (!live::available()) {
        return false;
    }
    if (auto s = live::session(); s && (s->have & SWOFTY_LIVE_SHAVE_TYPE) && s->type >= 1 && s->type <= 18)
        g_live_type = int(s->type);
    std::vector<momentum::Result> out;
    std::vector<momentum::Result> every; // the order, for the checks
    bool flag = false, running = false;
    int at_flag = 0;
    std::set<int> taken;
    for (const Driver &driver : g_drivers) {
        auto car = live::car(driver.car);
        if (!car || !live::has(*car, SWOFTY_LIVE_HAVE_CLASS)) {
            if (countable(driver))
                return false;
            continue;
        }
        uint8_t status = car->result_status;
        if (status < 2) {
            continue; // 0 invalid, 1 inactive: not in this race
        }
        momentum::Result result;
        result.stat = driver.stat_id;
        result.team = driver.team;
        result.position = car->position;
        // 2 running, 3 finished, 4 DNF, 5 DSQ, 6 not classified, 7 retired
        result.finished = status == 2 || status == 3;
        result.outcome = status == 5 ? 2 : status == 6 ? 3 : (status == 4 || status == 7) ? 1 : 0;
        running = running || status == 2;
        flag = flag || status == 3;
        at_flag += status == 3 ? 1 : 0;
        if (result.finished) {
            if (result.position <= 0)
                return false;
            if (!taken.insert(result.position).second) {
                g_unsettled = "two cars on one position";
                return false; // the game is re-sorting: the last good reading stands
            }
        }
        every.push_back(result);
        if (countable(driver))
            out.push_back(result);
    }
    if (out.empty()) {
        return false;
    }
    g_flag_seen = g_flag_seen || flag;
    g_results_from = "live state";
    g_at_flag = at_flag;
    g_in_race = int(every.size());
    bool row = in_a_row(every);
    if (flag && !running && row) {
        g_final_results = out;
        g_final = true;
        g_unsettled.clear();
    } else if (flag) {
        g_unsettled = running ? "cars still running" : "positions not in a row";
    }
    g_last_results = std::move(out);
    g_last_in_row = row;
    return true;
}

// The classification from the line crossings: the most crossings first, then the order they crossed in. A car that
// stopped crossing the line (more than two and a half of the leader's laps since its last crossing) has retired; a
// lapped car still crossing it is classified behind. Used when the live state has nothing.
inline void read_crossing_results()
{
    std::vector<const Driver *> order;
    int most = 0;
    for (const Driver &driver : g_drivers) {
        if (countable(driver)) {
            order.push_back(&driver);
            most = std::max(most, driver.crossings);
        }
    }
    if (most == 0) {
        return;
    }
    auto now = Clock::now();
    double lap = std::chrono::duration<double>(now - g_race_started).count() / std::max(1, most);
    std::stable_sort(order.begin(), order.end(), [](const Driver *a, const Driver *b) {
        if (a->crossings != b->crossings) {
            return a->crossings > b->crossings;
        }
        return a->last_position < b->last_position;
    });
    std::vector<momentum::Result> out;
    int position = 0;
    for (const Driver *driver : order) {
        momentum::Result result;
        result.stat = driver->stat_id;
        result.team = driver->team;
        bool stopped = driver->crossings < most &&
                       (driver->crossings == 0 ||
                        (lap > 0 && std::chrono::duration<double>(now - driver->last_crossing).count() > 2.5 * lap));
        result.finished = !stopped;
        result.outcome = stopped ? 1 : 0;
        result.position = result.finished ? ++position : 0;
        out.push_back(result);
    }
    g_last_results = std::move(out);
    g_leader_crossings = most;
    if (g_session_laps > 0 && most >= g_session_laps) {
        g_flag_seen = true;
    }
    g_results_from = "line crossings";
}

// Twice a second in a career race. Once the live state has given a classification, a frame without one (the game
// re-sorting) keeps it; the line crossings are only for a session the live state gives nothing for.
inline void watch_results()
{
    if (g_career.empty() || !g_race_session) {
        return;
    }
    if (read_live_results()) {
        return;
    }
    if (std::string_view(g_results_from) != "live state") {
        read_crossing_results();
    }
}

inline void forget_results()
{
    g_last_results.clear();
    g_final_results.clear();
    g_final = false;
    g_flag_seen = false;
    g_results_from = "nothing";
    g_unsettled.clear();
    g_leader_crossings = 0;
    g_live_type = -1;
    g_at_flag = g_in_race = 0;
    g_last_in_row = false;
}

// What a race left to count when its session ended (taken at session.end; counted once the end's reason is known).
struct RaceToCount {
    bool due = false;
    std::vector<momentum::Result> results;
    const char *from = "nothing";
    std::string why_not;   // "" when it can count
    int type = -1;         // its session type: the live state's; -1 unknown
    int session = -1;      // the session it ran in (g_session_serial)
    bool sprint = false;   // the event context said it was the sprint
    size_t field = 0;      // the cars on the grid
};
inline RaceToCount g_to_count;

// The session is ending: what the race leaves to count. The latest reading is taken first (results that arrive in the
// session's last frames).
inline void take_race()
{
    g_to_count = RaceToCount{};
    if (g_career.empty() || !g_race_session)
        return;
    watch_results();
    RaceToCount &r = g_to_count;
    r.due = true;
    r.field = g_drivers.size();
    r.type = g_live_type;
    r.session = g_session_serial;
    r.sprint = g_session_type == int(events::SessionType::sprint);
    r.from = g_results_from;
    bool live_source = std::string_view(g_results_from) == "live state";
    if (g_last_results.empty()) {
        r.why_not = "no classification";
    } else if (!g_flag_seen) {
        r.why_not = "it did not run to the flag";
    } else if (live_source && g_final) {
        r.results = g_final_results;
    } else if (live_source) {
        // settled at the end: unique positions in a row over every car, most of the field at the flag
        if (!g_last_in_row)
            r.why_not = "the classification wasn't settled (" +
                        (g_unsettled.empty() ? std::string("positions not in a row") : g_unsettled) + ")";
        else if (g_at_flag * 2 < g_in_race)
            r.why_not = "the classification wasn't settled (" + std::to_string(g_in_race - g_at_flag) + " of " +
                        std::to_string(g_in_race) + " cars not at the flag)";
        else
            r.results = g_last_results;
    } else {
        r.results = g_last_results; // the line crossings, with the race run to its length
    }
    // 0.1.0 fix: a result without most of the field identified says nothing about the field
    if (r.why_not.empty() && (r.results.size() < 2 || r.results.size() * 2 < r.field))
        r.why_not = "too few drivers identified (" + std::to_string(r.results.size()) + " of " + std::to_string(r.field) + ")";
}

// the race's identity: its weekend, its session type and its result
inline std::string race_key(const RaceToCount &r)
{
    std::string text;
    for (const momentum::Result &x : r.results)
        text += std::to_string(x.stat) + ":" + std::to_string(x.position) + ":" + std::to_string(x.outcome) + ",";
    char buffer[48];
    std::snprintf(buffer, sizeof buffer, "%d:%08x", r.type, momentum::text_hash(text));
    return g_book.weekend + "|" + buffer;
}

inline void log_results(const std::vector<momentum::Result> &results)
{
    for (const momentum::Result &result : results) {
        auto record = g_book.records.find(result.stat);
        if (record != g_book.records.end()) {
            std::string at = result.finished      ? "P" + std::to_string(result.position)
                             : result.outcome == 2 ? std::string("disqualified")
                             : result.outcome == 3 ? std::string("not classified")
                                                   : std::string("out");
            log("[driver-momentum]   stat %u %s: streak %+.2f, usual P%.1f, %s", unsigned(result.stat), at.c_str(),
                double(record->second.streak), double(record->second.average), momentum::recent_text(record->second).c_str());
        }
    }
}

// Counts the race taken at the session's end. reason: why the session ended (session.end's a: 0 it ended, 1 a
// restart, 2 unloading while open; -1 not told).
inline void count_race(int reason = -1)
{
    RaceToCount r = std::move(g_to_count);
    g_to_count = RaceToCount{};
    if (!r.due)
        return;
    if (reason == 1) {
        log("[driver-momentum] race not counted: the session was restarted");
        return;
    }
    if (!r.why_not.empty()) {
        log("[driver-momentum] race not counted: %s (%zu car(s) from the %s)", r.why_not.c_str(), r.results.size(), r.from);
        return;
    }
    std::string key = race_key(r);
    if (std::find(g_book.counted.begin(), g_book.counted.end(), key) != g_book.counted.end()) {
        log("[driver-momentum] race already counted (the same race closed twice): left out");
        return;
    }
    int finished = 0;
    for (const momentum::Result &x : r.results)
        finished += x.finished ? 1 : 0;
    momentum::Pending &first = g_book.first;
    bool same_weekend = first.valid && first.key.rfind(g_book.weekend + "|", 0) == 0;
    if (r.sprint) {
        // the game said so: a sprint, never the weekend's race
        momentum::score_race(g_book, r.results, true);
        log("[driver-momentum] sprint counted: weekend '%s', %d of %zu finished, from the %s", g_book.weekend.c_str(),
            finished, r.results.size(), r.from);
    } else if (same_weekend && first.type >= 1 && first.type == r.type && first.session >= 0 && first.session == r.session) {
        // the race run again in the same session (a restart after the flag): the new one replaces the first. A sprint
        // and a race are two sessions, even when the game gives both the same type
        g_book.records = first.before;
        g_book.races = first.races_before;
        g_book.weekend_scored = false;
        momentum::score_race(g_book, r.results, false);
        first.key = key;
        first.results = r.results;
        log("[driver-momentum] weekend '%s' ran its race again (session type %d): this one replaces the first",
            g_book.weekend.c_str(), r.type);
    } else if (same_weekend) {
        // a second race this weekend: the first was the sprint
        g_book.records = first.before;
        g_book.races = first.races_before;
        g_book.weekend_scored = false;
        momentum::score_race(g_book, first.results, true);
        momentum::score_race(g_book, r.results, false);
        first = momentum::Pending{};
        log("[driver-momentum] second race of weekend '%s': the first counted again as a sprint, this one as the race "
            "(%d of %zu finished, from the %s)",
            g_book.weekend.c_str(), finished, r.results.size(), r.from);
    } else if (g_book.weekend_scored) {
        log("[driver-momentum] weekend '%s' already counted its race and sprint: this one is left out",
            g_book.weekend.c_str());
        return;
    } else {
        first.valid = true;
        first.key = key;
        first.type = r.type;
        first.session = r.session;
        first.results = r.results;
        first.before = g_book.records;
        first.races_before = g_book.races;
        momentum::score_race(g_book, r.results, false);
        log("[driver-momentum] race counted: weekend '%s', %d of %zu finished, from the %s, session type %d",
            g_book.weekend.c_str(), finished, r.results.size(), r.from, r.type);
    }
    g_book.counted.push_back(key);
    log_results(r.results);
    save_book("race");
}

// The driver_momentum.form service's answer (g_mutex held). 0: the driver's form (and streak); 1: no form for that
// driver; 2: no weekend now. 0.1.0 fix: only for a book that belongs to now (a loaded career, or a Grand Prix while its
// session runs), and a stat id in range (65546 used to read as driver 10).
inline int form_answer(uint32_t driver_stat, float *form, float *streak)
{
    if (g_book.weekend.empty() || g_book_file.empty() || (g_career.empty() && !g_armed))
        return 2;
    if (driver_stat > 0xffff)
        return 1;
    float s = 0;
    if (!g_career.empty())
        if (auto record = g_book.records.find(uint16_t(driver_stat)); record != g_book.records.end())
            s = record->second.streak;
    if (streak)
        *streak = s;
    auto found = g_book.forms.find(uint16_t(driver_stat));
    if (form)
        *form = found != g_book.forms.end() ? found->second : 0.0f;
    return found != g_book.forms.end() ? 0 : 1;
}

} // namespace driver_form
