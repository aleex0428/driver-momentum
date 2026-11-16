// SPDX-License-Identifier: MIT
// Each driver's experience and awareness from the loaded game database (driver_statistics), for their regularity.
// They are all read once per game run on a thread of their own; the game's thread only looks them up. The grid
// waits a few seconds for the read at the start of the first session (driver_form.cpp), then a driver it has not
// reached reads as unknown (regularity 1).
// Adapted from Silly Season's game_pool.inl. Read only: nothing is written to the database.
#pragma once
#include <swofty/mixin.hpp>

#include <swofty/db.hpp>

#include "momentum.hpp"

#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

namespace driver_form::ratings {

using namespace swofty;

inline std::string text_of(const db::Value &v)
{
    if (v.s)
        return v.s;
    if (v.tag == SWOFTY_DB_INT || v.tag == SWOFTY_DB_BOOL)
        return std::to_string(v.i);
    if (v.tag == SWOFTY_DB_FLOAT)
        return std::to_string(v.f);
    return {};
}

inline std::string str(const std::string &key)
{
    db::Value v{};
    return db::get(key, &v) == db::Result::ok ? text_of(v) : std::string();
}

inline long long num(const std::string &key)
{
    db::Value v{};
    if (db::get(key, &v) != db::Result::ok)
        return 0;
    if (v.s)
        return std::strtoll(v.s, nullptr, 10);
    return v.tag == SWOFTY_DB_FLOAT ? static_cast<long long>(v.f) : v.i;
}

// "@v2@minx:0,maxx:10,miny:0,maxy:100,0:0.66,0:0.66,0.9:0.8,1:0.8" -> the value at the start, out of 100
inline float curve_start(const std::string &c)
{
    size_t at = c.rfind('@');
    std::string body = at == std::string::npos ? c : c.substr(at + 1);
    float maxy = 100;
    float first = -1;
    size_t pos = 0;
    while (pos <= body.size()) {
        size_t comma = body.find(',', pos);
        std::string part = body.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
        size_t colon = part.find(':');
        if (colon != std::string::npos) {
            std::string k = part.substr(0, colon), v = part.substr(colon + 1);
            if (k == "maxy")
                maxy = std::strtof(v.c_str(), nullptr);
            else if (first < 0 && !k.empty() && (std::isdigit(static_cast<unsigned char>(k[0])) || k[0] == '.'))
                first = std::strtof(v.c_str(), nullptr);
        }
        if (comma == std::string::npos)
            break;
        pos = comma + 1;
    }
    float value = first * maxy;
    return first >= 0 && value > 0 && value <= 100 ? value : -1;
}

inline std::mutex g_lock;
inline std::atomic<int> g_state{0};                          // 0 not started, 1 being read, 2 ready
inline std::unordered_map<uint32_t, momentum::Ratings> g_by_stat;     // filled by the background read
inline std::unordered_map<std::string, momentum::Ratings> g_by_label; // the same by entrants.drivers row

// A statistics holder's experience and awareness at the start of their curves: the 2026 set, else the default one,
// else the first. Background thread only.
inline momentum::Ratings read_holder(int index)
{
    momentum::Ratings out;
    std::string h = "driver_statistics.driver_statistics.DriverStatisticsHolder_" + std::to_string(index);
    long long sets = num(h + "._StatisticsDataSet_instancecount_");
    long long pick = -1;
    for (long long s = 0; s < sets && s < 64; ++s) {
        std::string name = str(h + ".StatisticsDataSet_" + std::to_string(s) + "._metadata_");
        if (name == "2026") {
            pick = s;
            break;
        }
        if (name == "default" || pick < 0)
            pick = s;
    }
    if (pick < 0)
        return out;
    std::string set = h + ".StatisticsDataSet_" + std::to_string(pick);
    long long count = num(set + "._Statistic_instancecount_");
    for (long long q = 0; q < count && q < 32; ++q) {
        std::string statistic = set + ".Statistic_" + std::to_string(q);
        std::string name = str(statistic + "._metadata_");
        if (name == "experience")
            out.experience = curve_start(str(statistic + ".evolution"));
        else if (name == "awareness")
            out.awareness = curve_start(str(statistic + ".evolution"));
    }
    return out;
}

// Starts the background read once the game database is there: every driver's ratings, read once per game run, so
// the game's thread never reads the database for them.
inline void start()
{
    int none = 0;
    if (!db::available() || !g_state.compare_exchange_strong(none, 1))
        return;
    std::thread([] {
        auto began = std::chrono::steady_clock::now();
        std::unordered_map<uint32_t, std::string> labels;
        db::Cursor c{};
        for (db::Result r = db::open("entrants.drivers", &c); r == db::Result::ok; r = db::step(&c)) {
            db::Value v{};
            const char *label = db::row_name(&c);
            if (label && db::cell(&c, "stat_id", &v) == db::Result::ok)
                labels.emplace(static_cast<uint32_t>(v.i), label);
        }
        std::unordered_map<std::string, momentum::Ratings> by_label;
        const std::string base = "driver_statistics.driver_statistics";
        long long n = num(base + "._DriverStatisticsHolder_instancecount_");
        for (long long i = 0; i < n && i < 4000; ++i) {
            std::string h = base + ".DriverStatisticsHolder_" + std::to_string(i);
            std::string who = str(h + ".driver");
            if (who.empty())
                who = str(h + "._metadata_");
            if (!who.empty() && !by_label.count(who))
                by_label.emplace(who, read_holder(int(i)));
        }
        std::unordered_map<uint32_t, momentum::Ratings> by_stat;
        for (const auto &[stat, label] : labels)
            if (auto found = by_label.find(label); found != by_label.end())
                by_stat.emplace(stat, found->second);
        {
            std::lock_guard lock(g_lock);
            g_by_stat = std::move(by_stat);
            g_by_label = std::move(by_label);
        }
        g_state = 2;
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - began).count();
        log("[driver-momentum] game database: %zu driver(s), %zu with ratings, read in the background in %.0f ms",
            labels.size(), g_by_stat.size(), ms);
    }).detach();
}

// The background read is still running.
inline bool reading() { return g_state.load() == 1; }

// A driver's experience and awareness; unknown (below 0) until the background read is done, or for a driver the
// database has no statistics for. A label from the event context ("lando_norris") is tried when the stat id has none.
inline momentum::Ratings of(uint16_t stat, const std::string &label_hint = {})
{
    if (g_state.load() != 2) {
        start();
        return {};
    }
    std::lock_guard lock(g_lock);
    if (auto found = g_by_stat.find(stat); found != g_by_stat.end())
        return found->second;
    if (auto found = g_by_label.find(label_hint); !label_hint.empty() && found != g_by_label.end())
        return found->second;
    return {};
}

} // namespace driver_form::ratings
