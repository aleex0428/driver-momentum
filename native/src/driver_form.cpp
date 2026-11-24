// SPDX-License-Identifier: MIT
// Driver Momentum, based on msyeett's Driver Form (MIT, https://github.com/constructor-mods/driver-form).
// Every driver the computer controls gets a weekend form. The form sets their pace and driving quality through the
// launcher's official live car levers (0.2.0: ai.grip_scale, engine.power_scale and the AI's racecraft slots, no writes
// to the game's memory), and brings bad patches and hot spells. A board names the drivers in and out of form.
// Driver Momentum adds the memory: in a career the form follows each driver's streak (their results against their
// usual and their teammate), their regularity (experience and awareness) and the dice; the Form Guide panel shows it
// all, and the driver_momentum.form service gives it to other addons.
// This file wires the other files together: the grid, the form roll, the career's book, the race results, the writes
// to the game, the patches, the messages, the panel and the service.
// Include mixin.hpp first: registry.hpp redefines SWOFTY_ADDON so the DLL exports SwoftyAddonRegister.
#include <swofty/mixin.hpp>

#include <swofty/db.hpp>
#include <swofty/events.hpp>
#include <swofty/hud.hpp>
#include <swofty/overlay.hpp>
#include <swofty/registry.hpp>
#include <swofty/services.hpp>
#include <swofty/storage.hpp>
#include <swofty/systems/career.hpp>
#include <swofty/systems/cars.hpp>
#include <swofty/systems/sessions.hpp>
#include <swofty/view.hpp>

#include <driver_momentum_api/form.hpp>

#include "ai_writes.hpp"
#include "build_drivers.hpp"
#include "form_guide.hpp"
#include "form_roll.hpp"
#include "form_state.hpp"
#include "grid.hpp"
#include "lap_events.hpp"
#include "messages.hpp"
#include "momentum.hpp"
#include "ownership.hpp"
#include "patches.hpp"
#include "results.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iterator>
#include <string>
#include <vector>

#ifndef DM_VERSION
#define DM_VERSION "dev"
#endif

using namespace swofty;
using namespace driver_form;

namespace {

bool g_guide_dirty = false; // the Form Guide's rows are to be built again (game thread, g_mutex held)
std::string g_hint_weekend;  // the career and weekend the Form Guide's reminder last showed for
// 0.1.0 fix: a race is counted once the session's end says why it ended (a restart counts nothing). The reason comes
// with the core's session.end event; the race taken at the end waits for it, at most a second.
std::atomic<int> g_end_reason{-1};
bool g_count_due = false;
Clock::time_point g_count_since;

// g_mutex held: the race taken at the session's end is counted, with the end's reason when it came
void settle_race(const char *when)
{
    if (!g_count_due)
        return;
    g_count_due = false;
    int reason = g_end_reason.exchange(-1);
    log("[driver-momentum] the race's session ended (%s, reason %d)", when, reason);
    count_race(reason);
}
Clock::time_point g_armed_at; // when the session was armed (the wait for the ratings counts from here)

// The Form Guide's rows from the grid and the book: best form first, your car last.
void publish_guide()
{
    g_guide_dirty = false;
    guide::Data data;
    data.career = !g_career.empty();
    data.level = momentum::name_of(g_level);
    data.level_roll = g_book.level;
    data.races = g_book.races;
    data.session = g_race_session ? "RACE" : g_session_kind == 1 ? "QUALIFYING" : g_session_kind == 0 ? "PRACTICE" : "";
    if (g_weekend_hint.rfind("round:", 0) == 0) {
        size_t colon = g_weekend_hint.rfind(':');
        data.round = std::atoi(g_weekend_hint.c_str() + colon + 1);
    }
    auto make_row = [&](const Driver &driver) {
        guide::Row row;
        row.label = driver.label;
        row.human = driver.human;
        row.form = driver.human ? 0.0f : driver.form;
        // 0.1.0 fix: the AI's target lap time against the game's, as the AI gets it (the same function, the moment's
        // shift included)
        row.pace = driver.human ? 0.0f : shift_of(driver);
        row.moment = driver.patch == Patch::hot       ? guide::Moment::hot
                     : driver.patch == Patch::bad     ? guide::Moment::bad
                     : driver.patch == Patch::moment  ? guide::Moment::mistake
                                                      : guide::Moment::none;
        const momentum::Record *record = nullptr;
        if (data.career && driver.verified) {
            auto found = g_book.records.find(driver.stat_id);
            if (found != g_book.records.end() && found->second.races > 0) {
                record = &found->second;
            }
        }
        if (record) {
            row.record = true;
            row.streak = record->streak;
            std::copy(std::begin(record->recent), std::end(record->recent), row.recent);
        }
        row.tag = driver.human ? "" : momentum::tag_of(record, row.form, driver.regularity);
        return row;
    };
    for (const Driver &driver : g_drivers)
        data.rows.push_back(make_row(driver));
    std::stable_sort(data.rows.begin(), data.rows.end(), [](const guide::Row &a, const guide::Row &b) {
        if (a.human != b.human) {
            return !a.human;
        }
        return a.form > b.form;
    });
    // 0.1.0: the compact view's three: in a race the cars just ahead and behind yours and your teammate; otherwise
    // the most and the least in form and your teammate
    const Driver *you = nullptr;
    for (const Driver &driver : g_drivers)
        if (driver.human) {
            you = &driver;
            break;
        }
    auto add = [&](const Driver *driver, const char *role) {
        if (!driver || data.compact.size() >= 3)
            return;
        guide::Row row = make_row(*driver);
        row.role = role;
        data.compact.push_back(std::move(row));
    };
    const Driver *mate = nullptr;
    if (you && !you->team.empty())
        for (const Driver &driver : g_drivers)
            if (&driver != you && driver.team == you->team)
                mate = &driver;
    auto position_of = [](const Driver &driver) {
        auto car = live::car(driver.car);
        return car && live::has(*car, SWOFTY_LIVE_HAVE_CLASS) ? car->position : 0;
    };
    int mine = you && g_race_session ? position_of(*you) : 0;
    if (mine > 0) {
        const Driver *ahead = nullptr, *behind = nullptr;
        for (const Driver &driver : g_drivers) {
            int at = position_of(driver);
            if (at == mine - 1)
                ahead = &driver;
            if (at == mine + 1)
                behind = &driver;
        }
        add(ahead, "AHEAD");
        add(behind, "BEHIND");
        if (mate != ahead && mate != behind)
            add(mate, "TEAMMATE");
    } else {
        const Driver *hot = nullptr, *cold = nullptr;
        for (const Driver &driver : g_drivers) {
            if (driver.human || !driver.verified)
                continue;
            if (driver.form > 0.05f && (!hot || driver.form > hot->form))
                hot = &driver;
            if (driver.form < -0.05f && (!cold || driver.form < cold->form))
                cold = &driver;
        }
        add(hot, "IN FORM");
        add(cold, "OUT OF FORM");
        if (mate != hot && mate != cold)
            add(mate, "TEAMMATE");
    }
    // the reminder of the key: once a weekend in this game run
    // 0.1.0 fix: the reminder belongs to a career and its weekend (two careers at the same round each get theirs)
    std::string reminder = g_career + "|" + g_book.weekend;
    bool hint = !g_drivers.empty() && reminder != g_hint_weekend;
    if (hint) {
        g_hint_weekend = reminder;
    }
    guide::set(std::move(data), hint);
}

// A Driver Career or My Team session (the same test Silly Season makes).
bool in_career(const events::Context &context)
{
    events::Mode mode = context.mode();
    if (mode == events::Mode::driver_career || mode == events::Mode::my_team) {
        return true;
    }
    if (mode != events::Mode::unknown) {
        return false;
    }
    career::State state{};
    return career::state(&state) == career::Result::ok && state.loaded &&
           (state.mode == SWOFTY_CAREER_DRIVER || state.mode == SWOFTY_CAREER_MY_TEAM);
}

// The loaded career's id: the save slot's, else the event context's. A career with neither is still new.
std::string career_id(const events::Context &context)
{
    std::string id = career::saves::current();
    if (id.empty() && context.career()) {
        id = context.career()->id;
    }
    return id.empty() ? std::string("unknown:new0") : id;
}

// The weekend the session belongs to: the career's season and round when the game says them (its session types do
// not say much, its round does), else the track.
std::string career_weekend(const events::Context &context)
{
    if (auto position = career::current_round(); position && position->round > 0) {
        return "round:" + std::to_string(position->season_index) + ":" + std::to_string(position->round);
    }
    if (const auto &session = context.session(); session && !session->track.id().empty()) {
        return "track:" + std::string(session->track.id());
    }
    if (const auto &weekend = context.weekend(); weekend && !weekend->track.id().empty()) {
        return "track:" + std::string(weekend->track.id());
    }
    return {};
}

// Gives back what the addon wrote and forgets the list. A car it never wrote to (yours) is not touched.
void stop()
{
    int released = 0;
    for (Driver &driver : g_drivers) {
        if (wrote_any(driver)) {
            clear_driver(driver);
            ++released;
        }
    }
    if (!g_drivers.empty())
        log("[driver-momentum] released %d car(s) the addon had changed", released);
    g_drivers.clear(); // destroying a Layer puts the game's value back
    g_armed = false;
    g_board_done = false;
    guide::clear();
}

// Gets ready for a new session. The list is built later, when the game has its drivers.
// 0.2.0: Driver Form's DLL in the game (both addons on in the profile)
bool driver_form_on() { return GetModuleHandleW(L"driver_form.dll") != nullptr; }

void arm()
{
    stop();
    g_last_tick = Clock::now(); // leaves the game time to take the cleared values before they are read as stock
    g_leave_ai = driver_form_on();
    if (g_leave_ai) {
        log("[driver-momentum] Driver Form (msyeett.driver-form) is also on: Driver Momentum leaves the AI to it and keeps "
            "only its forms and the Form Guide; turn one of them off on the Addons page");
        hud::toast("DRIVER FORM IS ALSO ON", "Driver Momentum leaves the AI to it. Turn one off on the Addons page.", 6,
                   "generic");
    }
    if (g_time_trial) {
        log("[driver-momentum] time trial, left alone");
        return;
    }
    if (g_other_mode) {
        log("[driver-momentum] not a career or a Grand Prix, left alone");
        return;
    }
    if (!strength_of(option<std::string>("form_strength"), g_strength)) {
        log("[driver-momentum] form strength off, normal AI");
        return;
    }
    g_patches_on = option<bool>("bad_patches");
    g_level = momentum::level_of(option<std::string>("momentum"));
    guide::set_key(option<std::string>("guide_key"));
    guide::set_size(option<std::string>("guide_size"));
    guide::g_hint_on = option<bool>("guide_hint");
    guide::g_compact_on = option<bool>("guide_compact");
    guide::g_reduced = option<std::string>("guide_motion") == "reduced";
    g_cars_at_crossing.fill(0);
    g_build_failures = 0;
    g_race_running = false;
    g_paused = false;
    g_check_control = true;
    g_armed = true;
    g_armed_at = Clock::now();
}

// Finds the cars you drive. Once yours, always yours: the game hands your car over in the pits.
// Asking who drives a car is slow, so every car is asked in one go: when the grid is built, then every 10 s.
// In between, the game tells us when it hands a car to you.
// 0.1.0 fix: a car is ours only once the game has said the computer drives it. A control that can't be read leaves
// the car alone (it used to make your car an AI car on the first tick when the context had said it was yours).
// 0.1.0 (13 Nov, measured in the game): reading every car's control in one tick took about 10 ms, a hitch every
// 10 s. Now the whole grid is read in one go only when it is built or a car is handed over; the rest of the time one
// car per tick (two per tick past 20 cars), so a pass still takes about 10 s and no tick pays more than ~0.5 ms.

// reads one car's control; true if it could be read
bool read_control(Driver &driver)
{
    auto control = cars::live::Car(driver.car).control();
    if (control && control->mode == sessions::Control::human) {
        g_player_cars.insert(driver.car);
    } else if (control) {
        driver.control_ai = true;
    }
    return bool(control);
}

// the end of a pass over the grid: `read` cars could be read
void finish_control_pass(int read)
{
    if (read) {
        g_control_misses = 0;
        g_control_blind = false;
    } else if (++g_control_misses == 3) {
        g_control_blind = true;
        log("[driver-momentum] no car's control can be read: %s", g_context_humans.empty()
                                                                      ? "nobody can tell which car is yours, so every car is left alone"
                                                                      : "the session's context tells your car, every other car gets its form");
    }
}

// the slow checks' worst times this session (ms), for the log line at session.end
double g_control_ms_max = 0, g_slice_ms_max = 0, g_system_ms_max = 0;

double ms_since(Clock::time_point from) { return std::chrono::duration<double, std::milli>(Clock::now() - from).count(); }

// the whole grid at once
void check_control()
{
    auto start = Clock::now();
    int read = 0;
    for (Driver &driver : g_drivers)
        read += read_control(driver) ? 1 : 0;
    finish_control_pass(read);
    g_control_next = 0;
    g_control_pass_read = 0;
    g_control_ms_max = std::max(g_control_ms_max, ms_since(start));
}

// a slice of it: one car a tick (two past 20 cars), a pass in about 10 s
void check_control_slice()
{
    size_t n = g_drivers.size();
    if (n == 0)
        return;
    auto start = Clock::now();
    size_t slice = (n + 19) / 20;
    for (size_t i = 0; i < slice; i++) {
        if (g_control_next >= n) {
            g_control_next = 0;
            g_control_pass_read = 0;
        }
        g_control_pass_read += read_control(g_drivers[g_control_next++]) ? 1 : 0;
        if (g_control_next >= n) {
            finish_control_pass(g_control_pass_read);
            g_control_next = 0;
            g_control_pass_read = 0;
        }
    }
    g_slice_ms_max = std::max(g_slice_ms_max, ms_since(start));
}

// Switches a driver between "yours, left alone" and "ours, form applied".
void update_ownership(Driver &driver, bool human)
{
    driver.human = human;
    g_guide_dirty = true;
    if (human) {
        bool had = wrote_any(driver);
        driver.layers.clear();
        clear_driver(driver);
        log("[driver-momentum] %s is yours, left alone%s", driver.label.c_str(), had ? " (what the addon had changed is given back)" : "");
        return;
    }
    start_layers(driver);
    log("[driver-momentum] car %u %s: form %+.2f, %+.2f s, quality %.2f", driver.car, driver.label.c_str(),
        double(driver.form), double(shift_of(driver)), double(quality_of(driver)));
}

// Holds the race clock while the game is paused: no patches start or end, and on resume every timer moves on by
// the length of the pause.
bool check_pause(Clock::time_point now)
{
    bool paused = view::paused();
    if (paused && !g_paused) {
        g_paused = true;
        g_paused_since = now;
        log("[driver-momentum] paused");
    } else if (!paused && g_paused) {
        g_paused = false;
        auto length = now - g_paused_since;
        g_race_started += length;
        for (Driver &driver : g_drivers) {
            driver.patch_until += length;
            driver.next_event += length;
        }
        log("[driver-momentum] resumed after %.0f s", std::chrono::duration<double>(length).count());
    }
    return paused;
}

// Runs twice a second: builds the grid when the session is ready, then keeps every driver's form written.
void frame(double, void *)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    auto now = Clock::now();
    if (g_count_due && (g_end_reason.load() >= 0 || now - g_count_since > std::chrono::seconds(1)))
        settle_race("frame");
    if (!g_armed) {
        return;
    }
    if (now - g_last_tick < std::chrono::milliseconds(500)) {
        return;
    }
    g_last_tick = now;
    // 0.2.0: the strip and the reminder follow the game's race HUD (empty when the core can't tell: shown)
    if (auto session = live::session())
        guide::g_hud_hidden = live::hud_visible(*session) == false;
    if (check_pause(now)) {
        return;
    }
    if (g_drivers.empty()) {
        // The first session of a game run waits a little for the drivers' ratings (read in the background), so its
        // forms have each driver's regularity in them.
        if (ratings::reading() && now - g_armed_at < std::chrono::seconds(8)) {
            return;
        }
        if (g_build_failures >= 10 && now - g_last_build_try < std::chrono::seconds(10)) {
            return;
        }
        g_last_build_try = now;
        if (build()) {
            g_build_failures = 0;
            g_guide_dirty = true;
        } else {
            g_drivers.clear();
            g_build_failures++;
        }
        return;
    }
    // Two checks: who drives each car (the whole grid when asked for, else a car a tick), and every 10 s, is the
    // live session still the one the grid was built in.
    if (g_check_control) {
        g_last_system_check = now - std::chrono::seconds(5);
        g_check_control = false;
        g_last_control_check = now;
        check_control();
    } else {
        check_control_slice();
    }
    if (now - g_last_system_check >= std::chrono::seconds(10)) {
        g_last_system_check = now;
        auto start = Clock::now();
        uint32_t generation = live_generation();
        g_system_ms_max = std::max(g_system_ms_max, ms_since(start));
        if (generation && g_live_generation && generation != g_live_generation) {
            // a new live session: the grid is built again (destroying the layers gives every car back first)
            log("[driver-momentum] a new live session (%u -> %u): the grid is built again", g_live_generation, generation);
            g_drivers.clear();
            return;
        }
    }
    for (Driver &driver : g_drivers) {
        bool human = !ours(driver);
        if (human != driver.human) {
            update_ownership(driver, human);
        }
        if (!human) {
            Patch before = driver.patch;
            tick_patch(driver, now);
            write_driver(driver);
            if (driver.patch != before) {
                g_guide_dirty = true; // the Form Guide shows the moment while it lasts
            }
        }
    }
    watch_results();
    // in a race the compact view's cars ahead and behind change all the time: its rows are read every tick
    if (g_race_session && guide::g_compact_on.load())
        g_guide_dirty = true;
    if (g_guide_dirty) {
        publish_guide();
    }
}

// ---- the driver_momentum.form service ----
int service_form(uint32_t driver_stat, float *form, float *streak)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return form_answer(driver_stat, form, streak);
}

int service_races()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_career.empty() ? 0 : g_book.races;
}

const DriverMomentumForm kFormService{sizeof(DriverMomentumForm), service_form, service_races};

// The overlay's paint callback: the Form Guide (render thread).
void paint(SwoftyPaint *paint, void *user)
{
    guide::paint(paint, user);
}

} // namespace

SWOFTY_ON_LOAD
{
    log("[driver-momentum] ready: version %s, based on Driver Form by msyeett", DM_VERSION);
    swofty::events::on_frame(frame);
    if (overlay::available()) {
        log("[driver-momentum] Form Guide: %s", overlay::name(overlay::on_paint(paint)));
    } else {
        log("[driver-momentum] Form Guide: no overlay on this core");
    }
    if (services::available()) {
        log("[driver-momentum] service %s v%u: %s", DriverMomentumForm::name, DriverMomentumForm::version,
            services::name(services::publish(kFormService)));
    }
}

SWOFTY_REGISTER
{
    events::on("session.start").after([](std::span<const std::byte>, const events::Context &context) {
        std::lock_guard<std::mutex> lock(g_mutex);
        settle_race("next session");
        ++g_session_serial;
        // Your car is learned again each session: the game hands it over right after this, before the grid is built.
        g_player_cars.clear();
        g_control_blind = false;
        g_control_misses = 0;
        g_context_entrants.clear();
        g_context_teams.clear();
        g_context_humans.clear();
        for (const auto &participant : context.participants()) {
            g_context_teams[participant.car] = std::string(participant.team.id());
            if (participant.human) {
                g_context_humans.insert(participant.car);
            }
            Entrant entrant;
            entrant.car = uint8_t(participant.car);
            std::string id(participant.driver.id());
            size_t slash = id.rfind('/');
            if (slash == std::string::npos) {
                entrant.label = id;
            } else {
                entrant.label = id.substr(slash + 1);
            }
            g_context_entrants.push_back(entrant);
        }
        const auto &view = context.session();
        if (!view) {
            g_session_kind = 2;
        } else if (view->type == events::SessionType::race || view->type == events::SessionType::sprint) {
            g_session_kind = 2;
        } else if (view->type == events::SessionType::qualifying) {
            g_session_kind = 1;
        } else {
            g_session_kind = 0;
        }
        events::Mode mode = context.mode();
        g_time_trial = (view && view->type == events::SessionType::time_trial) || mode == events::Mode::time_trial;
        g_session_laps = view ? view->laps : 0;
        g_session_type = view ? int(view->type) : -1;
        // Driver Momentum: the career's book, or the Grand Prix's weekend
        bool career = !g_time_trial && in_career(context);
        // 0.1.0 fix: only a career and a Grand Prix get forms. A mode the core can't name counts as a Grand Prix only
        // when no career is loaded (older cores name no mode); split screen, story and F1 World are left alone.
        g_other_mode = !career && !g_time_trial && mode != events::Mode::grand_prix && mode != events::Mode::unknown;
        if (career) {
            std::string id = career_id(context);
            follow_career(id);
            load_book(id);
            g_weekend_hint = career_weekend(context);
        } else {
            load_book("");
            g_weekend_hint.clear();
        }
        ratings::start();
        log("[driver-momentum] session.start: %zu entrants in the event context, session '%s', kind %d, %s %s%s%s",
            g_context_entrants.size(), view ? view->session.c_str() : "?", g_session_kind,
            career ? "career" : "Grand Prix", career ? g_career.c_str() : "",
            g_weekend_hint.empty() ? "" : ", weekend ", g_weekend_hint.c_str());
        arm();
        g_race_session = false;
        forget_results();
    });
    // 0.1.0 fix: a career unloaded takes its book with it (the service answers nothing for a career that isn't there)
    events::subscribe(SWOFTY_EVENT_CAREER_UNLOADED, [](const SwoftyEvent *, void *) {
        std::lock_guard<std::mutex> lock(g_mutex);
        settle_race("career unloaded");
        if (!g_career.empty()) {
            log("[driver-momentum] career unloaded: its book is put away");
            g_book = momentum::Book{};
            g_book_file.clear();
            g_career.clear();
        }
    });
    // 0.1.0 fix: why the session ended (0 it ended, 1 a restart, 2 unloading while open)
    events::subscribe(SWOFTY_EVENT_SESSION_END, [](const SwoftyEvent *e, void *) {
        g_end_reason = int(e->a);
        std::lock_guard<std::mutex> lock(g_mutex);
        settle_race("session.end");
    });
    // A restart fires race.start again with no session.end before it.
    events::on("race.start").after([](std::span<const std::byte>, const events::Context &) {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_session_kind = 2; // the Time Trial mark stays: a race start never makes one a race
        arm();
        g_race_running = true;
        g_race_started = Clock::now();
        g_race_session = true;
        forget_results(); // a restart starts the count again
    });
    events::on<sessions::events::Lap>().after([](const sessions::events::Lap &event, const events::Context &) {
        std::lock_guard<std::mutex> lock(g_mutex);
        on_lap(event.car.index());
    });
    // The game hands a car to you: it's yours from now on, the next tick leaves it alone.
    events::on<sessions::events::PlayerControl>().after(
        [](const sessions::events::PlayerControl &event, const events::Context &) {
            if (event.to != sessions::Control::human) {
                return;
            }
            std::lock_guard<std::mutex> lock(g_mutex);
            if (g_player_cars.insert(event.car.index()).second) {
                log("[driver-momentum] car %u handed to you", event.car.index());
            }
        });
    events::on<sessions::events::PitStop>().after([](const sessions::events::PitStop &event, const events::Context &) {
        std::lock_guard<std::mutex> lock(g_mutex);
        on_pit(event.car);
    });
    events::on("session.end").after([](std::span<const std::byte>, const events::Context &) {
        std::lock_guard<std::mutex> lock(g_mutex);
        log("[driver-momentum] session.end: releasing %zu driver(s)", g_drivers.size());
        log("[driver-momentum] timing: whole-grid control check max %.1f ms, a car a tick max %.2f ms, driver system "
            "check max %.1f ms", g_control_ms_max, g_slice_ms_max, g_system_ms_max);
        g_control_ms_max = g_slice_ms_max = g_system_ms_max = 0;
        // the race as the session leaves it; counted once the end's reason is known (settle_race)
        take_race();
        if (g_to_count.due) {
            g_count_due = true;
            g_count_since = Clock::now();
            if (g_end_reason.load() >= 0)
                settle_race("session.end");
        }
        forget_results();
        g_race_session = false;
        stop();
    });
}

SWOFTY_ADDON()
