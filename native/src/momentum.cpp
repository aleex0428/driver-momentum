// SPDX-License-Identifier: MIT
// Driver Momentum's engine (see momentum.hpp).
#include "momentum.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace momentum {

namespace {

// How much the streak counts at each level, and how much of the roll is left beside it.
struct Weights {
    float streak; // the streak's share of the form
    float dice;   // the roll's size (times the driver's regularity)
};

Weights weights_of(Level level)
{
    switch (level) {
    case Level::light:
        return {0.30f, 0.85f};
    case Level::strong:
        return {0.70f, 0.55f};
    case Level::normal:
        return {0.50f, 0.70f};
    case Level::off:
        break;
    }
    return {0.0f, 1.0f};
}

// The streak: how much one race moves it, and how much of it the next race keeps.
constexpr float kGain = 0.42f, kKeep = 0.70f;
constexpr float kSprintGain = 0.22f, kSprintKeep = 0.85f;
// The moving averages: how much one race counts.
constexpr float kAverageStep = 0.30f, kSprintAverageStep = 0.15f;
// The places that make a full swing: beating your usual by this much moves the streak by the whole gain.
constexpr float kOwnSpan = 5.0f, kMateSpan = 4.0f;

float uniform(std::mt19937 &random, float low, float high)
{
    return std::uniform_real_distribution<float>(low, high)(random);
}

// The original Driver Form's roll for one driver, by the third they fell in: 0 in form, 1 out of form, 2 neutral.
float third_roll(int third, std::mt19937 &random)
{
    if (third == 0)
        return uniform(random, 0.6f, 1.0f);
    if (third == 1)
        return uniform(random, -1.0f, -0.6f);
    return uniform(random, -0.15f, 0.15f);
}

float form_from(const Book &book, const Entrant &entrant, float roll, Level level)
{
    if (level == Level::off)
        return roll;
    // A book with no records yet (a Grand Prix, a career's first weekend) has no streak to share the form with:
    // the roll keeps the original's size, times the driver's regularity.
    Weights w = book.records.empty() ? Weights{0.0f, 1.0f} : weights_of(level);
    float streak = 0;
    if (auto found = book.records.find(entrant.stat); found != book.records.end())
        streak = found->second.streak;
    return std::clamp(roll * w.dice * regularity(entrant.ratings) + streak * w.streak, -1.0f, 1.0f);
}

std::string clean_line(std::string_view text)
{
    std::string out(text);
    for (char &c : out)
        if (c == '\n' || c == '\r')
            c = ' ';
    return out;
}

} // namespace

Level level_of(std::string_view name)
{
    if (name == "off")
        return Level::off;
    if (name == "light")
        return Level::light;
    if (name == "strong")
        return Level::strong;
    return Level::normal;
}

const char *name_of(Level level)
{
    switch (level) {
    case Level::off:
        return "off";
    case Level::light:
        return "light";
    case Level::strong:
        return "strong";
    case Level::normal:
        break;
    }
    return "normal";
}

float regularity(const Ratings &ratings)
{
    float sum = 0;
    int count = 0;
    for (float value : {ratings.experience, ratings.awareness}) {
        if (value > 0 && value <= 100) {
            sum += value;
            count++;
        }
    }
    if (!count)
        return 1.0f;
    return std::clamp(1.0f + (75.0f - sum / float(count)) / 50.0f, 0.7f, 1.3f);
}

void roll_weekend(Book &book, const std::vector<Entrant> &grid, Level level, std::mt19937 &random)
{
    book.forms.clear();
    book.weekend_scored = false;
    book.counted.clear();
    book.first = Pending{};
    book.level = name_of(level);
    std::vector<const Entrant *> ai;
    for (const Entrant &entrant : grid)
        if (!entrant.human)
            ai.push_back(&entrant);
    // A third of the grid in form, a third out, the rest neutral: the original's roll, now the dice beside the streak.
    std::vector<size_t> order(ai.size());
    for (size_t i = 0; i < order.size(); i++)
        order[i] = i;
    std::shuffle(order.begin(), order.end(), random);
    size_t third = ai.size() / 3;
    for (size_t i = 0; i < order.size(); i++) {
        int which = i < third ? 0 : i < third * 2 ? 1 : 2;
        const Entrant &entrant = *ai[order[i]];
        book.forms[entrant.stat] = form_from(book, entrant, third_roll(which, random), level);
    }
}

void fill_weekend(Book &book, const std::vector<Entrant> &grid, Level level, std::mt19937 &random)
{
    for (const Entrant &entrant : grid) {
        if (entrant.human || book.forms.count(entrant.stat))
            continue;
        int which = std::uniform_int_distribution<int>(0, 2)(random);
        book.forms[entrant.stat] = form_from(book, entrant, third_roll(which, random), level);
    }
}

bool score_race(Book &book, const std::vector<Result> &results, bool sprint)
{
    bool any_finished = false;
    for (const Result &result : results)
        any_finished = any_finished || (result.finished && result.position > 0);
    if (!any_finished)
        return false;
    float gain = sprint ? kSprintGain : kGain, keep = sprint ? kSprintKeep : kKeep;
    float step = sprint ? kSprintAverageStep : kAverageStep;
    // Each driver's measures against the record as it stood before this race.
    std::map<uint16_t, Record> before = book.records;
    for (const Result &result : results) {
        Record &record = book.records[result.stat]; // 0.1.0 fix: stat 0 is Carlos Sainz
        const Record &old = before[result.stat];
        if (!sprint) {
            for (int i = kRecent - 1; i > 0; i--)
                record.recent[i] = record.recent[i - 1];
            record.recent[0] = result.finished && result.position > 0 ? result.position
                               : result.outcome == 2               ? kRecentDsq
                               : result.outcome == 3               ? kRecentNc
                                                                   : 0;
            record.races++;
        }
        if (!result.finished || result.position <= 0)
            continue; // a retirement leaves the streak and the averages as they were
        // Against their own usual finish.
        float sum = 0;
        int parts = 0;
        if (old.average > 0) {
            sum += std::clamp((old.average - float(result.position)) / kOwnSpan, -1.0f, 1.0f);
            parts++;
        }
        // Against their teammate, by more or less than usual. The first race together only sets the usual.
        const Result *mate = nullptr;
        if (!result.team.empty()) {
            for (const Result &other : results) {
                if (&other != &result && other.team == result.team && other.finished && other.position > 0) {
                    mate = &other;
                    break;
                }
            }
        }
        if (mate) {
            float gap = float(mate->position - result.position);
            if (old.mate_races > 0) {
                sum += std::clamp((gap - old.mate_gap) / kMateSpan, -1.0f, 1.0f);
                parts++;
                record.mate_gap = old.mate_gap + (gap - old.mate_gap) * step;
            } else {
                record.mate_gap = gap;
            }
            record.mate_races = old.mate_races + 1;
        }
        float swing = parts ? sum / float(parts) : 0.0f;
        record.streak = std::clamp(old.streak * keep + swing * gain, -1.0f, 1.0f);
        record.average = old.average > 0 ? old.average + (float(result.position) - old.average) * step
                                         : float(result.position);
    }
    if (!sprint) {
        book.races++;
        book.weekend_scored = true;
    }
    return true;
}

const char *tag_of(const Record *record, float form, float regularity)
{
    if (record && record->races > 0) {
        if (record->streak >= kTagStreak)
            return "HOT STREAK";
        if (record->streak <= -kTagStreak)
            return "SLUMP";
    }
    if (form >= 0.6f)
        return "FLYING";
    if (form <= -0.6f)
        return "STRUGGLING";
    if (regularity <= 0.8f)
        return "STEADY";
    if (regularity >= 1.2f)
        return "ERRATIC";
    return "";
}

std::string recent_text(const Record &record, int count)
{
    std::string out;
    for (int i = 0; i < kRecent && i < count; i++) {
        int result = record.recent[i];
        if (result == kRecentNone)
            break;
        if (!out.empty())
            out += ' ';
        out += result == 0 ? std::string("R") : result == kRecentDsq ? std::string("DQ") : result == kRecentNc ? std::string("NC")
                                                                                                          : "P" + std::to_string(result);
    }
    return out;
}

uint32_t text_hash(std::string_view text)
{
    uint32_t h = 2166136261u;
    for (char c : text) {
        h ^= static_cast<unsigned char>(c);
        h *= 16777619u;
    }
    return h;
}

const char *name_of(ReadStatus status)
{
    switch (status) {
    case ReadStatus::ok:
        return "ok";
    case ReadStatus::empty:
        return "empty";
    case ReadStatus::unknown_version:
        return "a newer version";
    case ReadStatus::corrupt:
        break;
    }
    return "damaged";
}

namespace {

std::string token(const std::string &s)
{
    if (s.empty())
        return "-";
    std::string out = s;
    for (char &c : out)
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t')
            c = '_';
    return out;
}

std::string untoken(const std::string &s) { return s == "-" ? std::string() : s; }

void write_record(std::ostringstream &out, const char *word, unsigned stat, const Record &r)
{
    char buffer[200];
    std::snprintf(buffer, sizeof buffer, "%s %u %.4f %.3f %.3f %d %d %d %d %d %d %d\n", word, stat, double(r.streak),
                  double(r.average), double(r.mate_gap), r.mate_races, r.races, r.recent[0], r.recent[1], r.recent[2],
                  r.recent[3], r.recent[4]);
    out << buffer;
}

bool read_record(std::istringstream &fields, unsigned &stat, Record &r)
{
    if (!(fields >> stat >> r.streak >> r.average >> r.mate_gap >> r.mate_races >> r.races >> r.recent[0] >> r.recent[1] >>
          r.recent[2] >> r.recent[3] >> r.recent[4]) ||
        stat >= 0x10000)
        return false;
    r.streak = std::clamp(r.streak, -1.0f, 1.0f);
    return true;
}

} // namespace

std::string to_text(const Book &book)
{
    std::ostringstream out;
    char buffer[160];
    out << "driver-momentum 2\n";
    out << "career " << clean_line(book.career) << '\n';
    out << "level " << clean_line(book.level) << '\n';
    out << "weekend " << clean_line(book.weekend) << '\n';
    out << "time " << book.weekend_time << '\n';
    out << "scored " << (book.weekend_scored ? 1 : 0) << '\n';
    out << "races " << book.races << '\n';
    for (const std::string &key : book.counted)
        out << "counted " << token(key) << '\n';
    for (const auto &[stat, form] : book.forms) {
        std::snprintf(buffer, sizeof buffer, "form %u %.4f\n", unsigned(stat), double(form));
        out << buffer;
    }
    for (const auto &[stat, r] : book.records)
        write_record(out, "rec", unsigned(stat), r);
    if (book.first.valid) {
        out << "first " << book.first.type << ' ' << token(book.first.key) << ' ' << book.first.races_before << '\n';
        for (const Result &r : book.first.results)
            out << "fres " << r.stat << ' ' << r.position << ' ' << (r.finished ? 1 : 0) << ' ' << r.outcome << ' '
                << token(r.team) << '\n';
        for (const auto &[stat, r] : book.first.before)
            write_record(out, "fbefore", unsigned(stat), r);
    }
    std::string body = out.str();
    size_t lines = static_cast<size_t>(std::count(body.begin(), body.end(), '\n'));
    std::snprintf(buffer, sizeof buffer, "end %zu %08x\n", lines, text_hash(body));
    return body + buffer;
}

ReadStatus read_text(std::string_view text, Book &book)
{
    book = Book{};
    if (text.empty())
        return ReadStatus::empty;
    std::istringstream input{std::string(text)};
    std::string line;
    if (!std::getline(input, line))
        return ReadStatus::empty;
    if (!line.empty() && line.back() == '\r')
        line.pop_back();
    if (line.rfind("driver-momentum ", 0) != 0) {
        // The original Driver Form's weekend.txt: "<base lap> <time>", then "<stat id> <form>" lines.
        std::istringstream legacy{std::string(text)};
        float key = 0;
        long long time = 0;
        if (!(legacy >> key >> time))
            return ReadStatus::corrupt;
        char buffer[48];
        std::snprintf(buffer, sizeof buffer, "lap:%.3f", double(key));
        book.weekend = buffer;
        book.weekend_time = time;
        int stat = 0;
        float form = 0;
        while (legacy >> stat >> form)
            if (stat >= 0 && stat < 0x10000)
                book.forms[uint16_t(stat)] = std::clamp(form, -1.0f, 1.0f);
        return ReadStatus::ok;
    }
    int version = std::atoi(line.c_str() + 16);
    if (version < 1)
        return ReadStatus::corrupt;
    if (version > 2)
        return ReadStatus::unknown_version;
    bool strict = version >= 2;
    bool ended = false;
    if (strict) {
        // the checksum covers every line before "end"
        size_t at = text.rfind("\nend ");
        if (at == std::string_view::npos)
            return ReadStatus::corrupt;
        std::string_view body = text.substr(0, at + 1);
        unsigned lines = 0, hash = 0;
        if (std::sscanf(std::string(text.substr(at + 5)).c_str(), "%u %x", &lines, &hash) != 2 ||
            lines != static_cast<unsigned>(std::count(body.begin(), body.end(), '\n')) || hash != text_hash(body))
            return ReadStatus::corrupt;
    }
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        size_t space = line.find(' ');
        std::string word = line.substr(0, space);
        std::string rest = space == std::string::npos ? std::string() : line.substr(space + 1);
        std::istringstream fields(rest);
        bool bad = false;
        if (word == "career") {
            book.career = rest;
        } else if (word == "level") {
            book.level = rest;
        } else if (word == "weekend") {
            book.weekend = rest;
        } else if (word == "time") {
            bad = !(fields >> book.weekend_time);
        } else if (word == "scored") {
            int scored = 0;
            bad = !(fields >> scored);
            book.weekend_scored = scored != 0;
        } else if (word == "races") {
            bad = !(fields >> book.races);
        } else if (word == "counted") {
            book.counted.push_back(untoken(rest));
        } else if (word == "form") {
            unsigned stat = 0;
            float form = 0;
            if (fields >> stat >> form && stat < 0x10000)
                book.forms[uint16_t(stat)] = std::clamp(form, -1.0f, 1.0f);
            else
                bad = true;
        } else if (word == "rec" || word == "fbefore") {
            unsigned stat = 0;
            Record r;
            if (read_record(fields, stat, r))
                (word == "rec" ? book.records : book.first.before)[uint16_t(stat)] = r;
            else
                bad = true;
        } else if (word == "first") {
            std::string key;
            bad = !(fields >> book.first.type >> key >> book.first.races_before);
            book.first.key = untoken(key);
            book.first.valid = !bad;
        } else if (word == "fres") {
            Result r;
            int finished = 0;
            std::string team;
            unsigned stat = 0;
            bad = !(fields >> stat >> r.position >> finished >> r.outcome >> team) || stat >= 0x10000;
            r.stat = uint16_t(stat);
            r.finished = finished != 0;
            r.team = untoken(team);
            if (!bad)
                book.first.results.push_back(r);
        } else if (word == "end") {
            ended = true;
        }
        if (bad && strict)
            return ReadStatus::corrupt; // format 2 is written whole: a line that doesn't read means a damaged file
    }
    if (strict && !ended)
        return ReadStatus::corrupt;
    return ReadStatus::ok;
}

} // namespace momentum
