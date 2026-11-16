// SPDX-License-Identifier: MIT
// The form and streak engine over whole careers, without the game: a made-up grid races seasons where the form
// changes the pace the way the addon does in the game, and the checks look at what comes out.
//
//   model_test.exe [careers] [seasons]
#include "../src/momentum.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <random>
#include <string>
#include <vector>

using namespace momentum;

namespace {

int g_checks = 0, g_failed = 0;

void check(bool ok, const char *what, double value = NAN)
{
    g_checks++;
    if (ok)
        return;
    g_failed++;
    if (std::isnan(value))
        std::printf("FAIL: %s\n", what);
    else
        std::printf("FAIL: %s (%.3f)\n", what, value);
}

struct SimDriver {
    Entrant entrant;
    float skill = 0; // seconds a lap better than the slowest driver
};

struct Grid {
    std::vector<SimDriver> drivers;
    std::map<std::string, float> car; // seconds a lap better than the slowest car
};

Grid make_grid(std::mt19937 &random, bool with_human)
{
    Grid grid;
    const char *teams[] = {"mclaren", "ferrari", "red_bull", "mercedes", "aston", "alpine",
                           "williams", "racing_bulls", "sauber", "haas", "cadillac"};
    for (int t = 0; t < 11; t++) {
        grid.car[teams[t]] = 1.6f - t * 0.16f;
        for (int seat = 0; seat < 2; seat++) {
            SimDriver d;
            d.entrant.stat = uint16_t(1 + t * 2 + seat);
            d.entrant.team = teams[t];
            d.entrant.ratings.experience = std::uniform_real_distribution<float>(45, 98)(random);
            d.entrant.ratings.awareness = std::uniform_real_distribution<float>(55, 95)(random);
            d.entrant.human = with_human && t == 6 && seat == 1;
            d.skill = std::uniform_real_distribution<float>(0.0f, 0.6f)(random);
            grid.drivers.push_back(d);
        }
    }
    return grid;
}

std::vector<Entrant> entrants(const Grid &grid)
{
    std::vector<Entrant> out;
    for (const SimDriver &d : grid.drivers)
        out.push_back(d.entrant);
    return out;
}

// One race: the pace from the car, the driver, the form (1.5 s a lap at full form, as "Normal" in the game) and luck.
std::vector<Result> race(const Grid &grid, const Book &book, std::mt19937 &random, float dnf_chance = 0.07f)
{
    std::normal_distribution<float> luck(0.0f, 0.45f);
    std::vector<std::pair<float, const SimDriver *>> order;
    std::vector<Result> results;
    for (const SimDriver &d : grid.drivers) {
        float form = 0;
        if (auto f = book.forms.find(d.entrant.stat); f != book.forms.end())
            form = f->second;
        float pace = grid.car.at(d.entrant.team) + d.skill + form * (form < 0 ? 0.9f : 1.5f) * 0.6f + luck(random);
        if (std::uniform_real_distribution<float>(0, 1)(random) < dnf_chance) {
            results.push_back({d.entrant.stat, d.entrant.team, 0, false});
            continue;
        }
        order.push_back({pace, &d});
    }
    std::sort(order.begin(), order.end(), [](auto &a, auto &b) { return a.first > b.first; });
    for (size_t i = 0; i < order.size(); i++)
        results.push_back({order[i].second->entrant.stat, order[i].second->entrant.team, int(i + 1), true});
    return results;
}

// ---- whole careers at each level ----
void careers(Level level, int count, int seasons)
{
    char what[160];
    long long forms = 0, in_form = 0, out_of_form = 0, tagged = 0, driver_races = 0;
    bool bounded = true;
    int longest_run = 0;
    double worst_bias = 0;
    for (int c = 0; c < count; c++) {
        std::mt19937 random(1000u * unsigned(level) + unsigned(c));
        Grid grid = make_grid(random, c % 2 == 0);
        Book book;
        std::map<uint16_t, int> run;            // races in a row with a strong streak of the same sign
        std::map<uint16_t, int> run_sign;
        std::map<uint16_t, double> streak_sum;  // for the bias over the career
        std::map<uint16_t, int> streak_count;
        for (int season = 0; season < seasons; season++) {
            for (int round = 0; round < 24; round++) {
                roll_weekend(book, entrants(grid), level, random);
                for (const auto &[stat, form] : book.forms) {
                    forms++;
                    bounded = bounded && std::isfinite(form) && form >= -1 && form <= 1;
                    if (form > 0.3f)
                        in_form++;
                    if (form < -0.3f)
                        out_of_form++;
                }
                if (round % 4 == 3)
                    score_race(book, race(grid, book, random), true);
                score_race(book, race(grid, book, random), false);
                for (const SimDriver &d : grid.drivers) {
                    const Record &r = book.records[d.entrant.stat];
                    bounded = bounded && std::isfinite(r.streak) && r.streak >= -1 && r.streak <= 1;
                    driver_races++;
                    int sign = r.streak >= kTagStreak ? 1 : r.streak <= -kTagStreak ? -1 : 0;
                    if (sign)
                        tagged++;
                    int &length = run[d.entrant.stat];
                    length = sign && sign == run_sign[d.entrant.stat] ? length + 1 : (sign ? 1 : 0);
                    run_sign[d.entrant.stat] = sign;
                    longest_run = std::max(longest_run, length);
                    if (book.races > 5) {
                        streak_sum[d.entrant.stat] += r.streak;
                        streak_count[d.entrant.stat]++;
                    }
                }
            }
        }
        for (const auto &[stat, sum] : streak_sum)
            worst_bias = std::max(worst_bias, std::fabs(sum / streak_count[stat]));
        std::snprintf(what, sizeof what, "%s: the book counts every race (%d of %d)", name_of(level), book.races,
                      seasons * 24);
        check(book.races == seasons * 24, what);
    }
    double in_share = double(in_form) / double(forms), out_share = double(out_of_form) / double(forms);
    double tagged_share = double(tagged) / double(driver_races);
    std::printf("  %-6s  in form %4.1f%%  out of form %4.1f%%  hot or slump %4.1f%%  longest run %2d races  "
                "worst bias %.2f\n",
                name_of(level), in_share * 100, out_share * 100, tagged_share * 100, longest_run, worst_bias);
    std::snprintf(what, sizeof what, "%s: forms and streaks stay between -1 and +1", name_of(level));
    check(bounded, what);
    std::snprintf(what, sizeof what, "%s: about a third of the grid in form", name_of(level));
    check(in_share > 0.20 && in_share < 0.45, what, in_share);
    std::snprintf(what, sizeof what, "%s: about a third of the grid out of form", name_of(level));
    check(out_share > 0.20 && out_share < 0.45, what, out_share);
    if (level == Level::off) {
        check(tagged_share >= 0, "off: streaks are still kept for the Form Guide");
        return;
    }
    std::snprintf(what, sizeof what, "%s: hot streaks and slumps happen, but not all the time", name_of(level));
    check(tagged_share > 0.02 && tagged_share < 0.40, what, tagged_share);
    std::snprintf(what, sizeof what, "%s: no streak lasts for ever (longest run of races)", name_of(level));
    check(longest_run <= 14, what, longest_run);
    std::snprintf(what, sizeof what, "%s: no driver is in a streak for a whole career (mean streak)", name_of(level));
    check(worst_bias < 0.30, what, worst_bias);
}

// ---- single behaviours ----
void behaviours()
{
    std::mt19937 random(7);
    // A driver from a midfield car who suddenly wins three in a row is on a hot streak; three last places, a slump.
    {
        Book book;
        std::vector<Result> usual;
        for (int i = 0; i < 20; i++)
            usual.push_back({uint16_t(i + 1), "team" + std::to_string(i / 2), i + 1, true});
        for (int r = 0; r < 6; r++)
            score_race(book, usual, false);
        check(std::fabs(book.records[9].streak) < 0.05f, "the same result every race: no streak",
              book.records[9].streak);
        for (int r = 0; r < 3; r++) {
            std::vector<Result> hot = usual;
            for (Result &x : hot) {
                if (x.stat == 9)
                    x.position = 1;
                else if (x.position < 9)
                    x.position++;
                if (x.stat == 12)
                    x.position = 20;
                else if (x.stat != 9 && x.position > 12)
                    x.position--;
            }
            score_race(book, hot, false);
        }
        check(book.records[9].streak >= kTagStreak, "three wins from P9: a hot streak", book.records[9].streak);
        check(std::string(tag_of(&book.records[9], 0.0f, 1.0f)) == "HOT STREAK", "three wins: tagged HOT STREAK");
        check(book.records[12].streak <= -kTagStreak, "three last places from P12: a slump", book.records[12].streak);
        check(std::string(tag_of(&book.records[12], 0.0f, 1.0f)) == "SLUMP", "three last places: tagged SLUMP");
        check(recent_text(book.records[9], 3) == "P1 P1 P1", "the recent results, newest first");
        // ... and it fades once the results are the usual again. Coming back down from three wins reads as a small
        // dip for a few races (the usual finish has moved up meanwhile), never as a slump, and then it is gone.
        for (int r = 0; r < 8; r++) {
            score_race(book, usual, false);
            check(book.records[9].streak > -kTagStreak, "back to the usual after a hot streak: no slump",
                  book.records[9].streak);
        }
        check(std::fabs(book.records[9].streak) < 0.35f, "the hot streak fades with usual results",
              book.records[9].streak);
        for (int r = 0; r < 8; r++)
            score_race(book, usual, false);
        check(std::fabs(book.records[9].streak) < 0.10f, "and after a while it is gone", book.records[9].streak);
        // A retirement leaves the streak where it was.
        Book dnf = book;
        dnf.records[9].streak = 0.6f;
        std::vector<Result> out = usual;
        for (Result &x : out)
            if (x.stat == 9)
                x.finished = false, x.position = 0;
        score_race(dnf, out, false);
        check(dnf.records[9].streak == 0.6f, "a retirement leaves the streak as it is");
        check(dnf.records[9].recent[0] == 0 && recent_text(dnf.records[9], 1) == "R", "a retirement shows as R");
        // A sprint moves the streak, but is not a race.
        Book sprint = book;
        int races = sprint.records[9].races;
        int recent = sprint.records[9].recent[0];
        std::vector<Result> win = usual;
        for (Result &x : win)
            if (x.stat == 9)
                x.position = 1;
            else if (x.position < 9)
                x.position++;
        sprint.weekend_scored = false;
        score_race(sprint, win, true);
        check(sprint.records[9].streak > book.records[9].streak, "a sprint win lifts the streak");
        check(sprint.records[9].races == races && sprint.records[9].recent[0] == recent,
              "a sprint is not kept as a race");
        check(!sprint.weekend_scored && sprint.races == book.races, "a sprint does not close the weekend");
        check(!score_race(sprint, {}, false), "an empty classification counts nothing");
    }
    // A teammate who is always five places ahead is no streak for either of them.
    {
        Book book;
        for (int r = 0; r < 24; r++) {
            std::vector<Result> results = {{1, "a", 3 + (r % 3), true}, {2, "a", 8 + (r % 3), true}};
            score_race(book, results, false);
        }
        check(std::fabs(book.records[1].streak) < 0.25f && std::fabs(book.records[2].streak) < 0.25f,
              "a teammate who is always faster: no streak for either", book.records[2].streak);
    }
    // Regularity from the ratings.
    check(std::fabs(regularity({}) - 1.0f) < 1e-6f, "no ratings: regularity 1");
    check(regularity({96, 92}) == 0.7f, "a veteran swings least", regularity({96, 92}));
    check(regularity({40, 55}) == 1.3f, "a rookie swings most", regularity({40, 55}));
    check(std::string(tag_of(nullptr, 0.0f, 0.7f)) == "STEADY" && std::string(tag_of(nullptr, 0.0f, 1.3f)) == "ERRATIC",
          "steady and erratic tags");
    check(std::string(tag_of(nullptr, 0.7f, 1.0f)) == "FLYING" &&
              std::string(tag_of(nullptr, -0.7f, 1.0f)) == "STRUGGLING",
          "flying and struggling tags");
    {
        std::vector<Entrant> grid;
        for (int i = 0; i < 20; i++) {
            Entrant e;
            e.stat = uint16_t(i + 1);
            e.ratings = i < 10 ? Ratings{96, 94} : Ratings{40, 50};
            grid.push_back(e);
        }
        double veteran = 0, rookie = 0;
        Book book;
        for (int w = 0; w < 400; w++) {
            roll_weekend(book, grid, Level::normal, random);
            for (const auto &[stat, form] : book.forms)
                (stat <= 10 ? veteran : rookie) += double(form) * form;
        }
        check(rookie > veteran * 2.0, "a rookie's form swings more than a veteran's", rookie / veteran);
    }
    // Level off is the original Driver Form: a third in form (0.6..1), a third out, the rest near 0, no streak.
    {
        std::vector<Entrant> grid;
        for (int i = 0; i < 21; i++) {
            Entrant e;
            e.stat = uint16_t(i + 1);
            e.ratings = Ratings{40, 40};
            grid.push_back(e);
        }
        Book book;
        book.records[1].streak = 1.0f;
        book.records[1].races = 5;
        roll_weekend(book, grid, Level::off, random);
        int high = 0, low = 0, middle = 0;
        for (const auto &[stat, form] : book.forms) {
            high += form >= 0.6f;
            low += form <= -0.6f;
            middle += std::fabs(form) <= 0.15f;
        }
        check(high == 7 && low == 7 && middle == 7, "off: the original's thirds");
        // A Grand Prix (no records): the original's size, only stretched or shrunk by regularity.
        Book gp;
        std::vector<Entrant> plain = grid;
        for (Entrant &e : plain)
            e.ratings = Ratings{};
        roll_weekend(gp, plain, Level::strong, random);
        high = low = 0;
        for (const auto &[stat, form] : gp.forms) {
            high += form >= 0.6f;
            low += form <= -0.6f;
        }
        check(high == 7 && low == 7, "a Grand Prix at any level: the original's thirds");
    }
    // Your car: a record (your teammate is measured against you), but never a form.
    {
        std::vector<Entrant> grid(2);
        grid[0].stat = 1;
        grid[0].team = "a";
        grid[0].human = true;
        grid[1].stat = 2;
        grid[1].team = "a";
        Book book;
        roll_weekend(book, grid, Level::normal, random);
        check(!book.forms.count(1) && book.forms.count(2), "your car gets no form");
        score_race(book, {{1, "a", 1, true}, {2, "a", 2, true}}, false);
        check(book.records.count(1) && book.records[2].mate_races == 1, "your result counts for your teammate");
        // A driver who joins mid weekend gets a form, and nobody else's changes.
        float before = book.forms[2];
        Entrant late;
        late.stat = 3;
        grid.push_back(late);
        fill_weekend(book, grid, Level::normal, random);
        check(book.forms.count(3) && book.forms[2] == before && !book.forms.count(1), "a late driver gets a form");
    }
    // The book as text and back, and the original's weekend.txt.
    {
        Book book;
        book.weekend = "track:monza";
        book.weekend_time = 1763000000;
        book.weekend_scored = true;
        book.races = 12;
        book.forms[7] = 0.25f;
        book.forms[44] = -0.875f;
        Record r;
        r.streak = -0.4f;
        r.average = 7.5f;
        r.mate_gap = -2.25f;
        r.mate_races = 9;
        r.races = 11;
        r.recent[0] = 3;
        r.recent[1] = 0;
        r.recent[2] = 12;
        book.records[44] = r;
        Book back;
        bool read = from_text(to_text(book), back);
        check(read && back.weekend == book.weekend && back.weekend_time == book.weekend_time && back.weekend_scored &&
                  back.races == 12 && back.forms.size() == 2 && std::fabs(back.forms[44] + 0.875f) < 1e-4f,
              "the book reads back");
        const Record &b = back.records[44];
        check(std::fabs(b.streak + 0.4f) < 1e-4f && std::fabs(b.average - 7.5f) < 1e-3f &&
                  std::fabs(b.mate_gap + 2.25f) < 1e-3f && b.mate_races == 9 && b.races == 11 && b.recent[0] == 3 &&
                  b.recent[1] == 0 && b.recent[2] == 12 && b.recent[3] == -1,
              "a record reads back");
        Book legacy;
        check(from_text("91.250 1763000000\n4 0.75\n16 -0.8\n", legacy) && legacy.weekend == "lap:91.250" &&
                  legacy.weekend_time == 1763000000 && legacy.forms.size() == 2 &&
                  std::fabs(legacy.forms[16] + 0.8f) < 1e-4f,
              "the original Driver Form's weekend.txt reads");
        Book junk;
        check(!from_text("", junk) && !from_text("hello", junk), "an empty or broken file reads as nothing");
        // A weekend key with a newline can't break the file.
        Book odd;
        odd.weekend = "track:a\nform 1 1";
        Book odd_back;
        from_text(to_text(odd), odd_back);
        check(odd_back.forms.empty(), "a weekend key can't add lines");
    }
}

} // namespace

int main(int argc, char **argv)
{
    int count = argc > 1 ? std::atoi(argv[1]) : 24;
    int seasons = argc > 2 ? std::atoi(argv[2]) : 6;
    std::printf("model_test: %d careers of %d seasons at each level\n", count, seasons);
    for (Level level : {Level::off, Level::light, Level::normal, Level::strong})
        careers(level, count, seasons);
    behaviours();
    if (g_failed) {
        std::printf("model_test: %d of %d checks FAILED\n", g_failed, g_checks);
        return 1;
    }
    std::printf("model_test: all %d checks passed\n", g_checks);
    return 0;
}
