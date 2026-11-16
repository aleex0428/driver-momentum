// SPDX-License-Identifier: MIT
// Driver Momentum's engine: each driver's weekend form from their streak, their regularity and a roll of the dice,
// and the streak from the races they have run. Plain C++ with no game calls, so tests can run whole careers.
//
// - Form: -1 (badly out of form) .. +1 (flying), what the addon turns into pace and driving quality.
// - Streak: -1 (a slump) .. +1 (a hot streak). It moves after each race: up when a driver finishes better than they
//   usually do and beats their teammate by more than they usually do, down when they don't. Both measures are a
//   driver's own, so a slow car or a faster teammate is no slump by itself. A retirement says nothing about the
//   driver and leaves it as it is. It fades by itself, so a streak needs results to last.
// - Regularity: how far a driver's form swings from weekend to weekend, from their experience and awareness. A
//   veteran swings less, a rookie more.
#pragma once
#include <cstdint>
#include <map>
#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace momentum {

// How much the streak counts. off: the original Driver Form (a third in form, a third out, no memory).
enum class Level { off, light, normal, strong };

Level level_of(std::string_view name); // "off", "light", "normal", "strong"; anything else is normal
const char *name_of(Level level);

// The game's ratings (0..100) that set a driver's regularity; below 0 when unknown.
struct Ratings {
    float experience = -1;
    float awareness = -1;
};

// How far a driver's form swings: 0.7 (a veteran) .. 1.3 (a rookie). 1 when the ratings are unknown.
float regularity(const Ratings &ratings);

constexpr int kRecent = 5; // the results a driver's record keeps

// One driver in one career.
struct Record {
    float streak = 0;     // -1 .. +1
    float average = 0;    // their recent finishing position (a moving average); 0 before their first finish
    float mate_gap = 0;   // places they usually finish ahead of their teammate (a moving average; below 0: behind)
    int mate_races = 0;   // races with a finished teammate behind mate_gap
    int races = 0;        // races counted, retirements included
    // the last results, newest first: the position, 0 a retirement, -1 none yet, -2 disqualified, -3 not classified
    int recent[kRecent]{};
    Record()
    {
        for (int &result : recent)
            result = -1;
    }
};

// A car on this weekend's grid.
struct Entrant {
    uint16_t stat = 0;  // the driver's stat id
    std::string team;   // anything that tells the teams apart; empty when unknown
    Ratings ratings;
    bool human = false; // yours: it has a record (your teammate is measured against you) but no form
};

constexpr int kRecentNone = -1, kRecentDsq = -2, kRecentNc = -3;

// A car's result in a race.
struct Result {
    uint16_t stat = 0;
    std::string team;
    int position = 0;   // 1 = the winner
    bool finished = true; // false: retired, disqualified or not classified
    int outcome = 0;      // 0.1.0 fix: 0 classified, 1 retired, 2 disqualified, 3 not classified
};

// 0.1.0 fix: the weekend's first race, kept in the book (not only in memory) until a second race says whether it was
// the sprint: closing the game between a sprint and its race no longer loses the race
struct Pending {
    bool valid = false;
    std::string key;                    // the race's identity (results.hpp)
    int type = -1;                      // its session type when the game said one; -1 unknown
    int session = -1;                   // the game run's session it ran in (not saved: -1 after a reload)
    std::vector<Result> results;
    std::map<uint16_t, struct Record> before; // the records as they were before it counted
    int races_before = 0;
};

// A career's memory, or a Grand Prix weekend's (no records then).
struct Book {
    std::string career;                 // 0.1.0 fix: the career the book belongs to ("" a Grand Prix)
    std::string level;                  // the Momentum level the weekend's forms were rolled with
    std::vector<std::string> counted;   // the races counted this weekend (their identities): never twice
    Pending first;                      // the weekend's first race while it may still be the sprint
    std::string weekend;                // the weekend the forms below belong to
    long long weekend_time = 0;         // when it began (seconds since 1970)
    bool weekend_scored = false;        // its race has been counted (a sprint does not set it)
    std::map<uint16_t, float> forms;    // this weekend's form, by stat id
    std::map<uint16_t, Record> records; // careers only
    int races = 0;                      // races counted in this career
};

// Rolls a new weekend's forms for the grid. With Level::off, the original Driver Form's roll.
void roll_weekend(Book &book, const std::vector<Entrant> &grid, Level level, std::mt19937 &random);

// The forms of drivers the weekend has no form for (a driver who joined mid weekend): rolled the same way.
void fill_weekend(Book &book, const std::vector<Entrant> &grid, Level level, std::mt19937 &random);

// Counts a race: every driver's streak, average and recent results. A sprint moves the streak half as much and
// is not kept in the recent results. Returns false when there was nothing to count.
bool score_race(Book &book, const std::vector<Result> &results, bool sprint);

// A streak this strong is a "HOT STREAK" or a "SLUMP".
constexpr float kTagStreak = 0.55f;

// What to call a driver this weekend, for the Form Guide: "HOT STREAK", "SLUMP", "FLYING", "STRUGGLING",
// "STEADY", "ERRATIC" or "" (nothing to say).
const char *tag_of(const Record *record, float form, float regularity);

// "P3 P1 R P2" from the recent results, newest first; "" when there are none.
std::string recent_text(const Record &record, int count = kRecent);

// The book as text and back. A Grand Prix book written by the original Driver Form ("weekend.txt": a key, a time,
// then stat id and form pairs) reads too, and so does format 1 (0.1.0 before its fixes).
// 0.1.0 fix: format 2 names its career and ends with its line count and a checksum, so a partial or foreign file is
// told apart from a book; a version above 2 is never taken for one.
enum class ReadStatus { ok, empty, unknown_version, corrupt };
const char *name_of(ReadStatus status);
std::string to_text(const Book &book);
ReadStatus read_text(std::string_view text, Book &book);
inline bool from_text(std::string_view text, Book &book) { return read_text(text, book) == ReadStatus::ok; }
// a short hash for file names and checksums (FNV-1a)
uint32_t text_hash(std::string_view text);

} // namespace momentum
