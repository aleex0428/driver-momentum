// SPDX-License-Identifier: MIT
// The plugin's career side without the game: the book of each career and where it is kept, a new career taking its
// own id, the weekend's forms, a race read from the live state and counted, a sprint found by the second race of its
// weekend, the line-crossing fallback, a race left before the flag and the Grand Prix's weekend.txt. The core's
// storage is an in-memory map and its live state the SDK's fake (swofty/fake_live.hpp).
//
// 0.1.0 fixes, each reproduced here first: your car's protection, what the addon writes and gives back, the modes,
// Carlos Sainz's stat id 0 and unknown cars, the book's format 2 and its safe move to a career's own id, the sprint
// kept over a reload, a race closed twice or restarted, provisional classifications, and the service's answers.
//
//   career_test.exe [verbose]
#include <swofty/mixin.hpp>

#include <swofty/fake_live.hpp>

#include "../src/ai_writes.hpp"
#include "../src/build_drivers.hpp"
#include "../src/ownership.hpp"
#include "../src/results.hpp"

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>

using namespace driver_form;

namespace {

int g_checks = 0, g_failed = 0;

void check(bool ok, const char *what)
{
    g_checks++;
    if (!ok) {
        g_failed++;
        std::printf("FAIL: %s\n", what);
    }
}

// ---- the core's storage, in memory ----
std::map<std::string, std::string> g_files;
bool g_fail_writes = false;  // every write fails
std::string g_lose_writes;   // writes to this file say ok but keep nothing

int st_available() { return 1; }
int st_read(const char *path, void *buf, size_t cap, size_t *len)
{
    auto found = g_files.find(path);
    if (found == g_files.end())
        return SWOFTY_ASSET_INVALID;
    *len = found->second.size();
    if (found->second.size() > cap)
        return SWOFTY_ASSET_INVALID;
    std::memcpy(buf, found->second.data(), found->second.size());
    return SWOFTY_ASSET_OK;
}
int st_write(const char *path, const void *data, size_t len)
{
    if (g_fail_writes)
        return SWOFTY_ASSET_INVALID;
    if (g_lose_writes == path)
        return SWOFTY_ASSET_OK;
    g_files[path] = std::string(static_cast<const char *>(data), len);
    return SWOFTY_ASSET_OK;
}
int st_append(const char *path, const void *data, size_t len)
{
    g_files[path].append(static_cast<const char *>(data), len);
    return SWOFTY_ASSET_OK;
}
int st_remove(const char *path) { return g_files.erase(path) ? SWOFTY_ASSET_OK : SWOFTY_ASSET_INVALID; }
int st_size(const char *path, uint64_t *out)
{
    auto found = g_files.find(path);
    if (found == g_files.end())
        return SWOFTY_ASSET_INVALID;
    *out = found->second.size();
    return SWOFTY_ASSET_OK;
}
int st_list(const char *, char *, size_t, size_t *len)
{
    *len = 0;
    return SWOFTY_ASSET_OK;
}
const SwoftyStorage kStorage = {st_available, st_read, st_write, st_append, st_remove, st_size, st_list};

bool g_quiet = true;
void log_(const char *fmt, ...)
{
    if (g_quiet)
        return;
    va_list a;
    va_start(a, fmt);
    std::vprintf(fmt, a);
    std::printf("\n");
    va_end(a);
}

// ---- a grid of six cars: three teams, your car is 5 ----
void make_grid(swofty::fake::Live &live, uint16_t first_stat = 10)
{
    g_drivers.clear();
    g_context_humans = {5};
    g_context_teams.clear();
    g_player_cars.clear();
    for (uint32_t car = 0; car < 6; car++) {
        Driver d;
        d.car = car;
        d.stat_id = uint16_t(first_stat + car);
        d.verified = true;
        d.label = "DRIVER" + std::to_string(car);
        d.team = "team" + std::to_string(car / 2);
        d.stock.base_lap_time = 91.25f;
        d.human = car == 5;
        g_drivers.push_back(std::move(d));
        auto &c = live.add_car(car, 0);
        c.have |= SWOFTY_LIVE_HAVE_CLASS;
    }
}

// a race the live state shows: `order` from the winner, `out` the cars that retired
void live_race(swofty::fake::Live &live, const std::vector<uint32_t> &order, const std::vector<uint32_t> &out,
               bool flag = true)
{
    std::vector<uint32_t> classified;
    for (uint32_t car : order)
        classified.push_back(car);
    for (uint32_t car : out)
        classified.push_back(car);
    live.set_order(classified);
    for (uint32_t car : order)
        live.car(car).result_status = flag ? 3 : 2;
    for (uint32_t car : out)
        live.car(car).result_status = 7;
}

// the session ends as the game ends a race: taken, then counted with the end's reason (0: it ended)
void close_race(int reason = 0)
{
    take_race();
    count_race(reason);
    forget_results();
}

const momentum::Record &rec(uint16_t stat) { return g_book.records[stat]; }

bool has_file(const std::string &f) { return g_files.count(f) != 0; }

// ---- your car's protection, and what the addon writes ----
void ownership(swofty::fake::Live &live)
{
    make_grid(live);
    g_control_blind = false;
    for (Driver &d : g_drivers)
        d.control_ai = false;
    // the context says car 5 is yours; nobody's control could be read yet
    check(!ours(g_drivers[5]), "your car (the context's) is never ours, its control unknown");
    check(!ours(g_drivers[0]), "a car whose control is unknown is left alone");
    g_drivers[0].control_ai = true;
    g_drivers[5].control_ai = true; // even if a read said the computer drives it
    check(ours(g_drivers[0]) && !ours(g_drivers[5]), "a car the game says the computer drives is ours; yours never");
    // the live state's player
    g_context_humans.clear();
    live.set_player(5);
    check(!ours(g_drivers[5]), "the live state's player is protected without the context");
    live.session().player = -1;
    live.session().have &= ~uint64_t(SWOFTY_LIVE_SHAVE_PLAYER);
    live.car(5).flags &= ~SWOFTY_LIVE_CAR_PLAYER;
    g_player_cars.insert(5); // the game handed it to you
    check(!ours(g_drivers[5]), "a car handed to you stays yours");
    g_player_cars.clear();
    // a core without control reads: only with the context telling your car
    g_drivers[0].control_ai = false;
    g_control_blind = true;
    check(!ours(g_drivers[0]), "blind and no context: every car is left alone");
    g_context_humans = {5};
    check(ours(g_drivers[0]) && !ours(g_drivers[5]), "blind with the context: the others get their form, yours doesn't");
    g_control_blind = false;

    // writes: only an AI car's profile, and only what was written is given back
    g_strength = {1.5f, 0.45f, 1.0f, 1.0f};
    std::vector<uint8_t> mem_ai(0x200, 0), mem_you(0x200, 0);
    float game_value = 77.0f;
    std::memcpy(mem_you.data() + 0x148, &game_value, 4);
    Driver &ai = g_drivers[0];
    Driver &you = g_drivers[5];
    ai.profile = reinterpret_cast<uintptr_t>(mem_ai.data());
    you.profile = reinterpret_cast<uintptr_t>(mem_you.data());
    ai.human = false;
    ai.form = 0.8f;
    you.human = true;
    write_driver(ai);
    write_driver(you);
    float ai_now = 0, you_now = 0;
    std::memcpy(&ai_now, mem_ai.data() + 0x148, 4);
    std::memcpy(&you_now, mem_you.data() + 0x148, 4);
    check(ai.wrote_profile && std::fabs(ai_now - (91.25f - 0.8f * 1.5f)) < 1e-3f, "the AI car's target lap time is written");
    check(!you.wrote_profile && you_now == 77.0f, "your car is never written");
    check(wrote_any(ai) && !wrote_any(you), "the addon knows what it wrote");
    clear_driver(you);
    std::memcpy(&you_now, mem_you.data() + 0x148, 4);
    check(you_now == 77.0f, "giving back never touches a car the addon didn't write (yours keeps the game's value)");
    clear_driver(ai);
    std::memcpy(&ai_now, mem_ai.data() + 0x148, 4);
    check(!ai.wrote_profile && std::fabs(ai_now - 91.25f) < 1e-4f, "the AI car gets the game's value back");
    ai.profile = you.profile = 0;
}

// ---- Carlos Sainz (stat 0) and cars without an identity ----
void identities(swofty::fake::Live &live)
{
    make_grid(live, 0); // stats 0..5: car 0 is Carlos Sainz
    g_drivers[3].verified = false; // a car whose id couldn't be read
    std::vector<momentum::Entrant> e = book_entrants();
    bool sainz = false, unknown = false;
    for (const momentum::Entrant &x : e) {
        sainz = sainz || x.stat == 0;
        unknown = unknown || x.stat == 3;
    }
    check(sainz, "Carlos Sainz (stat 0) gets a form");
    check(!unknown, "a car without a read identity gets none");
    load_book("ids-career");
    g_weekend_hint = "round:0:1";
    give_weekend_forms();
    check(g_book.forms.count(0) == 1 && g_drivers[3].form == 0.0f, "the book has Sainz's form and not the unknown car's");
    g_race_session = true;
    live_race(live, {0, 1, 2, 3, 4, 5}, {});
    watch_results();
    close_race();
    check(g_book.races == 1 && rec(0).recent[0] == 1 && !g_book.records.count(3), "Sainz's win counts; the unknown car's result doesn't");
    // a grid mostly unknown: the race says nothing
    for (int i = 1; i < 6; i++)
        g_drivers[size_t(i)].verified = false;
    g_weekend_hint = "round:0:2";
    give_weekend_forms();
    live_race(live, {0, 1, 2, 3, 4, 5}, {});
    watch_results();
    close_race();
    check(g_book.races == 1, "a race with one driver identified of six isn't counted");
    g_race_session = false;
}

// ---- the book: format 2, foreign files, a newer version, a safe move ----
void book_files(swofty::fake::Live &live)
{
    make_grid(live);
    check(career_file("a:b") != career_file("a_b"), "two ids that clean to the same name have their own books");
    // round trip
    momentum::Book b;
    b.career = "x";
    b.level = "strong";
    b.weekend = "round:1:2";
    b.races = 3;
    b.forms[0] = 0.5f;
    b.records[7].streak = 0.25f;
    b.records[7].recent[0] = momentum::kRecentDsq;
    b.counted = {"round:1:2|15:00ab"};
    b.first.valid = true;
    b.first.key = "round:1:2|15:00ab";
    b.first.type = 15;
    b.first.results = {{7, "team a", 1, true, 0}, {8, "", 0, false, 2}};
    b.first.before[7].streak = 0.1f;
    b.first.races_before = 2;
    momentum::Book r;
    check(momentum::read_text(momentum::to_text(b), r) == momentum::ReadStatus::ok && r.career == "x" && r.level == "strong" &&
              r.races == 3 && r.forms.count(0) && std::fabs(r.records[7].streak - 0.25f) < 1e-3f &&
              r.records[7].recent[0] == momentum::kRecentDsq && r.counted.size() == 1 && r.first.valid && r.first.type == 15 &&
              r.first.results.size() == 2 && r.first.results[0].team == "team_a" && r.first.results[1].outcome == 2 &&
              r.first.races_before == 2 && std::fabs(r.first.before[7].streak - 0.1f) < 1e-3f,
          "format 2 reads back whole");
    std::string text = momentum::to_text(b);
    check(momentum::read_text(text.substr(0, text.size() / 2), r) == momentum::ReadStatus::corrupt, "a cut file is damaged, not a book");
    std::string edited = text;
    edited.replace(edited.find("races 3"), 7, "races 9");
    check(momentum::read_text(edited, r) == momentum::ReadStatus::corrupt, "a changed line breaks the checksum");
    check(momentum::read_text("driver-momentum 999\nrec 10 0.5\n", r) == momentum::ReadStatus::unknown_version,
          "a newer version is never taken for a book");
    check(momentum::read_text("driver-momentum 1\nweekend w\nrec 10 0.5 4 0 0 1 1 -1 -1 -1 -1\n", r) == momentum::ReadStatus::ok &&
              r.records.count(10),
          "format 1 still reads");
    std::string recent = momentum::recent_text(b.records[7]);
    check(recent == "DQ", "a disqualification shows as DQ");

    // a newer version's file is never overwritten
    g_files[career_file("newer")] = "driver-momentum 3\nwhatever\n";
    load_book("newer");
    check(g_book_file.empty(), "a career whose book is newer runs without one");
    save_book("test");
    check(g_files[career_file("newer")] == "driver-momentum 3\nwhatever\n", "and its file is left as it was");

    // another career's book under this name (a collision of the old names)
    momentum::Book other;
    other.career = "someone-else";
    other.races = 5;
    g_files[career_file("mine")] = momentum::to_text(other);
    load_book("mine");
    check(g_book.races == 0 && has_file(career_file("mine") + ".bad"), "another career's book is kept aside, not used");

    // the move of a new career's book: the write fails, then works
    g_book_file.clear();
    load_book("slot7:new1");
    g_book.races = 4;
    save_book("test");
    std::string provisional_file = g_book_file;
    g_fail_writes = true;
    follow_career("slot7");
    check(g_career == "slot7:new1" && has_file(provisional_file), "a failed write keeps the provisional book and its id");
    g_fail_writes = false;
    g_lose_writes = career_file("slot7");
    follow_career("slot7");
    check(g_career == "slot7:new1" && has_file(provisional_file), "a write that doesn't hold keeps it too");
    g_lose_writes.clear();
    follow_career("slot7");
    check(g_career == "slot7" && g_book.races == 4 && !has_file(provisional_file) && has_file(career_file("slot7")),
          "the move is tried again and the provisional book goes once the new one holds");
    // a damaged book under the new id
    g_book_file.clear();
    load_book("slot8:new1");
    g_book.races = 6;
    save_book("test");
    std::string prov8 = g_book_file;
    g_files[career_file("slot8")] = "driver-momentum 2\nrace";
    follow_career("slot8");
    check(g_career == "slot8" && g_book.races == 6 && has_file(career_file("slot8") + ".bad") && !has_file(prov8),
          "a damaged book under the new id is kept aside and the provisional one moves in");
}

// ---- races: a sprint over a reload, a race closed twice, a restart, provisional readings ----
void race_cases(swofty::fake::Live &live)
{
    make_grid(live);
    g_book_file.clear();
    load_book("sprint-career");
    g_weekend_hint = "round:2:5";
    give_weekend_forms();
    momentum::Book before = g_book;
    g_race_session = true;
    live_race(live, {0, 2, 1, 3, 4, 5}, {});
    watch_results();
    close_race();
    check(g_book.races == 1 && g_book.first.valid, "the weekend's first race counts and waits in the book");
    // the game closes: the book is read again
    g_book = momentum::Book{};
    g_book_file.clear();
    load_book("sprint-career");
    check(g_book.first.valid && g_book.first.results.size() == 6, "the first race is still there after a reload");
    give_weekend_forms(); // the same weekend
    g_race_session = true;
    live_race(live, {1, 0, 2, 3, 5}, {4});
    watch_results();
    close_race();
    check(g_book.races == 1 && !g_book.first.valid, "the second race: the first was the sprint, one race in the book");
    {
        momentum::Book expect = before;
        std::vector<momentum::Result> sprint, race;
        for (auto [car, pos] : std::vector<std::pair<int, int>>{{0, 1}, {2, 2}, {1, 3}, {3, 4}, {4, 5}, {5, 6}})
            sprint.push_back({uint16_t(10 + car), "team" + std::to_string(car / 2), pos, true, 0});
        for (auto [car, pos] : std::vector<std::pair<int, int>>{{1, 1}, {0, 2}, {2, 3}, {3, 4}, {5, 5}})
            race.push_back({uint16_t(10 + car), "team" + std::to_string(car / 2), pos, true, 0});
        race.push_back({14, "team2", 0, false, 1});
        momentum::score_race(expect, sprint, true);
        momentum::score_race(expect, race, false);
        check(std::fabs(expect.records[11].streak - rec(11).streak) < 1e-3f && std::fabs(expect.records[10].streak - rec(10).streak) < 1e-3f,
              "sprint, reload, race: the streaks are as if the sprint had been known");
    }
    // the same race closed twice
    int races_now = g_book.races;
    float streak = rec(11).streak;
    live_race(live, {1, 0, 2, 3, 5}, {4});
    watch_results();
    close_race();
    check(g_book.races == races_now && rec(11).streak == streak, "the same race closed twice counts once");
    // a restart: the session ended for a restart
    g_weekend_hint = "round:2:6";
    give_weekend_forms();
    live_race(live, {3, 0, 1, 2, 4, 5}, {});
    watch_results();
    close_race(1);
    check(g_book.races == races_now && !g_book.first.valid, "a session that ended in a restart counts nothing");
    // the race run again with the same session type: it replaces the first
    live.session().type = 15;
    live.session().have |= SWOFTY_LIVE_SHAVE_TYPE;
    live_race(live, {3, 0, 1, 2, 4, 5}, {});
    watch_results();
    close_race();
    check(g_book.races == races_now + 1 && g_book.first.type == 15, "the race counts once");
    live_race(live, {0, 3, 1, 2, 4, 5}, {});
    watch_results();
    close_race();
    check(g_book.races == races_now + 1 && rec(10).recent[0] == 1 && rec(13).recent[0] == 2 && rec(13).recent[1] != 1,
          "run again (the same session type): the new race replaces the first, no sprint is made");
    live.session().have &= ~uint64_t(SWOFTY_LIVE_SHAVE_TYPE);
    // a weekend without a sprint, then the next: nothing is carried over
    g_weekend_hint = "round:2:7";
    give_weekend_forms();
    check(!g_book.first.valid && g_book.counted.empty(), "a new weekend starts clean");

    // provisional readings: one car at the flag, five still running, two in P2
    g_weekend_hint = "round:2:8";
    give_weekend_forms();
    races_now = g_book.races;
    live.set_order({0, 1, 2, 3, 4, 5});
    live.car(0).result_status = 3;
    for (uint32_t car = 1; car < 6; car++)
        live.car(car).result_status = 2;
    live.car(2).position = 2; // two cars in P2
    watch_results();
    close_race();
    check(g_book.races == races_now, "one car at the flag, five running and two in P2: not counted");
    // most of the field still running at the end, positions in a row: not settled
    live.set_order({0, 1, 2, 3, 4, 5});
    live.car(0).result_status = 3;
    live.car(1).result_status = 3;
    watch_results();
    close_race();
    check(g_book.races == races_now, "most cars still running at the end: not counted");
    // a disqualification and a car not classified
    live_race(live, {0, 1, 2, 3}, {});
    live.set_order({0, 1, 2, 3, 4, 5});
    live.car(4).result_status = 5;
    live.car(5).result_status = 6;
    watch_results();
    close_race();
    check(g_book.races == races_now + 1 && rec(14).recent[0] == momentum::kRecentDsq && rec(15).recent[0] == momentum::kRecentNc,
          "disqualified and not classified are kept apart from a retirement");
    g_race_session = false;
}

// ---- the service's answers ----
void service(swofty::fake::Live &live)
{
    make_grid(live);
    g_book_file.clear();
    load_book("service-career");
    g_weekend_hint = "round:0:1";
    give_weekend_forms();
    float form = 9, streak = 9;
    check(form_answer(10, &form, &streak) == 0 && form == g_book.forms[10], "a driver's form");
    check(form_answer(65536 + 10, &form, &streak) == 1, "65546 isn't driver 10");
    g_book = momentum::Book{};
    g_book_file.clear();
    g_career.clear();
    g_armed = false;
    check(form_answer(10, &form, &streak) == 2, "no career loaded and no session: no weekend");
}

} // namespace

int main(int argc, char **argv)
{
    g_quiet = !(argc > 1 && std::string(argv[1]) == "verbose");
    static SwoftyApi api{};
    api.version = SWOFTY_API_VERSION;
    api.log = log_;
    api.storage = &kStorage;
    swofty::api = &api;
    swofty::fake::Live live;
    live.install();

    // ---- a new career, under its provisional id ----
    make_grid(live);
    g_level = momentum::Level::normal;
    load_book("slot1:new1");
    g_weekend_hint = "round:0:1";
    give_weekend_forms();
    check(g_career == "slot1:new1" && g_book_file == career_file("slot1:new1"), "a career's book is careers/<id>-<hash>.txt");
    check(has_file(career_file("slot1:new1")), "the new weekend is saved at once");
    check(g_book.forms.size() == 5 && !g_book.forms.count(15), "five forms: your car has none");
    check(g_drivers[0].form == g_book.forms[10], "every driver gets the book's form");
    float kept = g_book.forms[11];
    give_weekend_forms(); // qualifying: the same weekend
    check(g_book.forms[11] == kept, "the same weekend keeps its forms");

    // ---- its race, from the live state ----
    g_race_session = true;
    live_race(live, {2, 0, 1, 5, 3}, {4});
    watch_results();
    check(std::string(g_results_from) == "live state" && g_flag_seen, "the live state gives the classification");
    close_race();
    check(g_book.races == 1 && g_book.weekend_scored, "the race is counted");
    check(rec(12).recent[0] == 1 && rec(10).recent[0] == 2 && rec(14).recent[0] == 0, "positions and the retirement");
    check(rec(15).races == 1, "your result is kept too (your teammate is measured against you)");
    check(rec(15).mate_races == 0 && rec(12).mate_races == 1 && rec(13).mate_races == 1, "teammates: only when both finished");
    g_race_session = false;

    // ---- the career gets its own id: the book follows ----
    follow_career("slot1");
    check(g_career == "slot1" && g_book_file == career_file("slot1"), "the book moves to the career's own id");
    check(has_file(career_file("slot1")) && !has_file(career_file("slot1:new1")), "the provisional file is gone");
    follow_career("slot1");
    follow_career("slot2");
    check(g_career == "slot1", "a career with its own id is not moved again");

    // ---- a sprint weekend: the second race makes the first a sprint ----
    g_weekend_hint = "round:0:2";
    give_weekend_forms();
    check(g_book.weekend == "round:0:2" && !g_book.weekend_scored, "round 2 is a new weekend");
    g_race_session = true;
    live_race(live, {0, 2, 1, 3, 4, 5}, {});
    watch_results();
    close_race();
    check(g_book.races == 2 && rec(10).recent[0] == 1, "the first race counts as a race until a second one comes");
    live_race(live, {1, 0, 2, 3, 5}, {4});
    watch_results();
    close_race();
    check(g_book.races == 2, "the second race: still two races in the book");
    check(rec(11).recent[0] == 1 && rec(11).recent[1] == 3, "the race is kept, the sprint is not");
    check(rec(14).recent[0] == 0 && rec(14).recent[1] == 0, "the race's retirement is kept");
    live_race(live, {3, 0, 1, 2, 5}, {4});
    watch_results();
    close_race();
    check(g_book.races == 2 && rec(13).recent[0] == 4, "a third race in a weekend is left out");

    // ---- a race left before the flag is not counted ----
    g_weekend_hint = "round:0:3";
    give_weekend_forms();
    live_race(live, {0, 1, 2, 3, 4, 5}, {}, false);
    watch_results();
    close_race();
    check(g_book.races == 2 && !g_book.weekend_scored, "a race without the chequered flag is not counted");

    // ---- no live state: the line crossings ----
    live.set_available(false);
    g_session_laps = 3;
    auto now = Clock::now();
    g_race_started = now - std::chrono::seconds(300); // a lap: 100 s
    int crossings[6] = {3, 3, 3, 2, 1, 3};
    int last_pos[6] = {2, 1, 3, 4, 6, 4};
    int ago[6] = {5, 4, 6, 40, 400, 7}; // seconds since each car last crossed the line
    for (Driver &d : g_drivers) {
        d.crossings = crossings[d.car];
        d.last_position = last_pos[d.car];
        d.last_crossing = now - std::chrono::seconds(ago[d.car]);
    }
    watch_results();
    check(std::string(g_results_from) == "line crossings" && g_flag_seen, "the line crossings give the classification");
    close_race();
    check(g_book.races == 3 && rec(11).recent[0] == 1 && rec(10).recent[0] == 2 && rec(15).recent[0] == 4 && rec(13).recent[0] == 5,
          "crossings: the leaders by order, a car a lap down still classified");
    check(rec(14).recent[0] == 0, "crossings: a car that stopped crossing the line has retired");
    live.set_available(true);
    g_session_laps = 0;
    // a lapped car two laps down but still crossing is no retirement (0.1.0 fix)
    g_weekend_hint = "round:0:3b";
    give_weekend_forms();
    live.set_available(false);
    g_session_laps = 3;
    for (Driver &d : g_drivers) {
        d.crossings = d.car == 4 ? 1 : 3;
        d.last_crossing = now - std::chrono::seconds(d.car == 4 ? 30 : 5);
    }
    watch_results();
    close_race();
    check(rec(14).recent[0] > 0, "crossings: two laps down but still crossing: classified, not retired");
    live.set_available(true);
    g_session_laps = 0;
    for (Driver &d : g_drivers)
        d.crossings = 0;

    // ---- the live state: a frame without the classification keeps the last one; a car not in the race is skipped ----
    g_weekend_hint = "round:0:3c";
    give_weekend_forms();
    live_race(live, {0, 1, 2, 3, 4, 5}, {});
    live.car(3).result_status = 0;
    watch_results();
    check(g_last_results.size() == 5 && std::string(g_results_from) == "live state", "a car the race does not have is skipped");
    std::vector<momentum::Result> kept_results = g_last_results;
    live.car(0).have &= ~SWOFTY_LIVE_HAVE_CLASS;
    watch_results();
    check(g_last_results.size() == kept_results.size() && g_last_results[0].position == kept_results[0].position &&
              std::string(g_results_from) == "live state",
          "a frame without the classification keeps the last one");
    live.car(0).have |= SWOFTY_LIVE_HAVE_CLASS;
    forget_results();

    // ---- the game closes; the career loads again ----
    int races = g_book.races;
    std::map<uint16_t, momentum::Record> records = g_book.records;
    g_book = momentum::Book{};
    g_book_file.clear();
    load_book("slot1");
    check(g_book.races == races && g_book.records.size() == records.size() &&
              std::fabs(g_book.records[11].streak - records[11].streak) < 1e-3f && g_book.records[11].recent[0] == records[11].recent[0],
          "the book reads back after a restart");

    // ---- another career at the same round: the first race of one is never the other's sprint ----
    g_weekend_hint = "round:0:4";
    give_weekend_forms();
    g_race_session = true;
    live_race(live, {0, 1, 2, 3, 4, 5}, {});
    watch_results();
    close_race();
    check(g_book.races == races + 1 && g_book.first.valid, "slot1's round 4 race is counted");
    // slot2's book of 0.1.0 before its fixes: format 1, under the old name
    g_files["careers/slot2.txt"] = "driver-momentum 1\nweekend round:0:4\ntime 1\nscored 1\nraces 7\n"
                                   "rec 10 0.3000 4.000 1.000 5 7 4 4 4 4 4\n";
    load_book("slot2");
    check(g_career == "slot2" && g_book.races == 7 && !g_book.first.valid, "slot2 has its own book, without slot1's first race");
    check(has_file(career_file("slot2")) && !has_file("careers/slot2.txt"), "an old book moves to its new name once it holds");
    give_weekend_forms();
    live_race(live, {5, 4, 3, 2, 1, 0}, {});
    watch_results();
    close_race();
    check(g_book.races == 7 && std::fabs(rec(10).streak - 0.3f) < 1e-4f && rec(10).recent[0] == 4,
          "slot2's weekend already had its race: nothing of slot1's is put back");
    load_book("slot1");
    check(g_book.races == races + 1, "slot1 reads back with its races");
    g_race_session = false;

    // ---- a Grand Prix: weekend.txt, the original's file and key ----
    g_files["weekend.txt"] = "91.250 " + std::to_string((long long)std::time(nullptr) - 60) + "\n10 0.75\n11 -0.8\n";
    load_book("");
    g_weekend_hint.clear();
    g_context_humans.clear();
    give_weekend_forms();
    check(g_career.empty() && g_book_file == "weekend.txt", "a Grand Prix uses weekend.txt");
    check(std::fabs(g_book.forms[10] - 0.75f) < 1e-4f && std::fabs(g_drivers[1].form + 0.8f) < 1e-4f,
          "the original Driver Form's weekend carries on");
    check(g_book.forms.size() == 6, "drivers it did not have get a form");
    g_race_session = true;
    live_race(live, {0, 1, 2, 3, 4, 5}, {});
    watch_results();
    close_race();
    check(g_book.races == 0 && g_book.records.empty(), "a Grand Prix keeps no records");
    long long t_now = (long long)std::time(nullptr);
    g_book.weekend_time = t_now - 2 * 24 * 3600;
    g_book.forms.erase(11);
    give_weekend_forms();
    check(g_book.weekend_time >= t_now - 5 && g_book.forms.size() == 6, "a Grand Prix weekend older than a day is new");
    g_race_session = false;

    // ---- 0.1.0 fixes ----
    ownership(live);
    identities(live);
    book_files(live);
    race_cases(live);
    service(live);

    live.uninstall();
    if (g_failed) {
        std::printf("career_test: %d of %d checks FAILED\n", g_failed, g_checks);
        return 1;
    }
    std::printf("career_test: all %d checks passed\n", g_checks);
    return 0;
}
