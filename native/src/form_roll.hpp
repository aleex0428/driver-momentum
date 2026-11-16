// SPDX-License-Identifier: MIT
// The weekend's forms and where they are kept. A career keeps a book of its own (careers/<id>-<hash>.txt): this
// weekend's forms and every driver's streak and recent results, so the form follows the season. A Grand Prix keeps
// only the weekend (weekend.txt, as the original Driver Form did), so it lasts through practice, qualifying and the race.
//
// 0.1.0 fixes: a career's file name carries a hash of its id (two ids that clean to the same name no longer share a
// book), the book says which career it is and is checked when read, a save is read back before anything relies on it,
// and moving a new career's book to its own id never deletes the only copy.
#pragma once
#include <swofty/mixin.hpp>

#include <swofty/storage.hpp>
#include <swofty/systems/career.hpp>

#include "form_state.hpp"
#include "momentum.hpp"

#include <cctype>
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>

namespace driver_form {

constexpr const char *kGrandPrixFile = "weekend.txt";
constexpr long long kWeekendLife = 24 * 3600; // a Grand Prix weekend older than this is a new weekend

// A new career is saved under a provisional id ("...:new1") until the game gives it its own.
inline bool provisional(const std::string &id) { return id.find(":new") != std::string::npos; }

inline std::string clean_name(const std::string &id)
{
    std::string name;
    for (char c : id)
        name += (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.') ? c : '_';
    return name;
}

// the career's book: its cleaned id and a hash of the id itself
inline std::string career_file(const std::string &id)
{
    char hash[12];
    std::snprintf(hash, sizeof hash, "%08x", momentum::text_hash(id));
    std::string name = clean_name(id);
    if (name.size() > 100)
        name.resize(100);
    return "careers/" + name + "-" + hash + ".txt";
}

// 0.1.0 before its fixes: the cleaned id alone
inline std::string legacy_career_file(const std::string &id)
{
    std::string name = clean_name(id);
    if (name.size() > 120)
        name.resize(120);
    return "careers/" + name + ".txt";
}

// Writes a book and reads it back: true only when the storage holds exactly that text.
inline bool write_verified(const std::string &file, const std::string &text)
{
    if (!storage::available())
        return false;
    storage::Result w = storage::write(file, text);
    std::string back;
    bool same = w == storage::Result::ok && storage::read(file, back) == storage::Result::ok && back == text;
    if (!same)
        log("[driver-momentum] book %s: the write didn't hold (%s)", file.c_str(), storage::name(w));
    return same;
}

inline void save_book(const char *why)
{
    if (g_book_file.empty() || !storage::available())
        return;
    g_book.career = g_career;
    bool ok = write_verified(g_book_file, momentum::to_text(g_book));
    log("[driver-momentum] book saved (%s): %s, %zu record(s), %d race(s): %s", why, g_book_file.c_str(),
        g_book.records.size(), g_book.races, ok ? "ok" : "NOT saved");
}

// Reads a career's book from `file`: ok only when it reads whole and, in format 2, names this career.
inline momentum::ReadStatus read_book(const std::string &file, const std::string &career, momentum::Book &out,
                                      bool *foreign = nullptr)
{
    std::string text;
    if (!storage::available() || storage::read(file, text) != storage::Result::ok)
        return momentum::ReadStatus::empty;
    momentum::ReadStatus status = momentum::read_text(text, out);
    if (status == momentum::ReadStatus::ok && !career.empty() && !out.career.empty() && out.career != career) {
        if (foreign)
            *foreign = true;
        out = momentum::Book{};
        return momentum::ReadStatus::corrupt;
    }
    return status;
}

// A file that can't be used is kept aside (never overwritten) for a report.
inline void keep_aside(const std::string &file, const char *why)
{
    std::string text;
    if (storage::available() && storage::read(file, text) == storage::Result::ok && !text.empty())
        storage::write(file + ".bad", text);
    log("[driver-momentum] book %s %s: kept aside as %s.bad", file.c_str(), why, file.c_str());
}

// Loads the book of a career ("" for a Grand Prix) unless it is the one already loaded.
inline void load_book(const std::string &career)
{
    std::string file = career.empty() ? std::string(kGrandPrixFile) : career_file(career);
    if (file == g_book_file)
        return;
    g_book = momentum::Book{};
    g_book_file = file;
    g_career = career;
    bool foreign = false;
    momentum::ReadStatus status = read_book(file, career, g_book, &foreign);
    if (status == momentum::ReadStatus::ok) {
        log("[driver-momentum] book loaded: %s, weekend '%s', %zu record(s), %d race(s)", file.c_str(),
            g_book.weekend.c_str(), g_book.records.size(), g_book.races);
        return;
    }
    if (status == momentum::ReadStatus::unknown_version) {
        // a newer Driver Momentum wrote it: never overwritten by this one
        log("[driver-momentum] book %s comes from a newer version: this career runs without a book", file.c_str());
        g_book = momentum::Book{};
        g_book_file.clear();
        return;
    }
    if (status == momentum::ReadStatus::corrupt)
        keep_aside(file, foreign ? "belongs to another career" : "doesn't read whole");
    g_book = momentum::Book{};
    // a book of 0.1.0 before its fixes, under the old name: read, saved under the new one, and only then removed
    if (!career.empty()) {
        std::string legacy = legacy_career_file(career);
        momentum::Book old;
        if (read_book(legacy, career, old) == momentum::ReadStatus::ok) {
            old.career = career;
            if (write_verified(file, momentum::to_text(old))) {
                storage::remove(legacy);
                log("[driver-momentum] book %s moved to %s", legacy.c_str(), file.c_str());
            }
            g_book = std::move(old);
            return;
        }
    }
    log("[driver-momentum] new book: %s", file.c_str());
}

// The career got its own id (a new career's first save): its book moves from the provisional one. The provisional
// file goes only once the new one reads back as this career's book; until then it stays, and the move is tried again
// at the next session.
inline void follow_career(const std::string &now)
{
    if (now.empty() || now == g_career || g_career.empty() || !provisional(g_career) || provisional(now))
        return;
    std::string old_file = g_book_file, new_file = career_file(now);
    momentum::Book there;
    bool foreign = false;
    momentum::ReadStatus status = read_book(new_file, now, there, &foreign);
    if (status == momentum::ReadStatus::ok) {
        // that id already has a book (a career loaded before): it wins; the provisional one is no longer needed
        log("[driver-momentum] career %s -> %s: that career already has a book", g_career.c_str(), now.c_str());
        g_book_file.clear();
        load_book(now);
        if (storage::available() && !old_file.empty() && old_file != g_book_file)
            storage::remove(old_file);
        return;
    }
    if (status == momentum::ReadStatus::unknown_version) {
        log("[driver-momentum] career %s -> %s: that career's book comes from a newer version; this one stays as it is",
            g_career.c_str(), now.c_str());
        return;
    }
    if (status == momentum::ReadStatus::corrupt)
        keep_aside(new_file, foreign ? "belongs to another career" : "doesn't read whole");
    momentum::Book moved = g_book;
    moved.career = now;
    if (!write_verified(new_file, momentum::to_text(moved))) {
        log("[driver-momentum] career %s -> %s: the book couldn't be written under the new id; it stays under the old "
            "one and moves at the next session",
            g_career.c_str(), now.c_str());
        return;
    }
    log("[driver-momentum] career %s -> %s: the book moves to the new id", g_career.c_str(), now.c_str());
    g_career = now;
    g_book = std::move(moved);
    g_book_file = new_file;
    if (storage::available() && !old_file.empty())
        storage::remove(old_file);
}

// The entrants as the engine sees them.
inline std::vector<momentum::Entrant> book_entrants()
{
    std::vector<momentum::Entrant> out;
    for (const Driver &driver : g_drivers) {
        if (!driver.verified || driver.stat_id >= 0xf000)
            continue; // 0.1.0 fix: Carlos Sainz (stat 0) is a driver like any other; an unknown car isn't
        momentum::Entrant entrant;
        entrant.stat = driver.stat_id;
        entrant.team = driver.team;
        entrant.ratings = driver.ratings;
        entrant.human = g_context_humans.count(driver.car) != 0 || g_player_cars.count(driver.car) != 0;
        out.push_back(entrant);
    }
    return out;
}

// The weekend this session belongs to: the career's round when the game says it, else the track's base lap time
// (the original Driver Form's key, which also tells a Grand Prix's weekends apart).
inline std::string weekend_key()
{
    if (!g_weekend_hint.empty())
        return g_weekend_hint;
    char buffer[48];
    std::snprintf(buffer, sizeof buffer, "lap:%.3f", g_drivers.empty() ? 0.0 : double(g_drivers[0].stock.base_lap_time));
    return buffer;
}

} // namespace driver_form
