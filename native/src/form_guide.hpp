// SPDX-License-Identifier: MIT
// The Form Guide: a drawer docked to the right edge of the screen, opened and closed with a key (F6 by default). It
// reads the grid like a thermal camera: every driver has a heat index from -100 (ice cold) to +100 (red hot) for this
// weekend, shown on a thermometer, with the time a lap it is worth; the whole grid's heat as an equaliser on top with
// the hottest and the coldest driver; and in a career each driver's trend over their last five races, drawn as a
// line, and those results as cells coloured by how hot they were. During a race, a driver in a hot spell, a bad patch
// or a mistake shows it live. It does not take the mouse or the keys from the game: drive on while it is open. At the
// first session of each weekend a tab slides out of the screen's edge for a few seconds with the key.
// The game's thread fills the rows (set); the render thread draws them.
#pragma once
#include <swofty/mixin.hpp>
#include <swofty/overlay.hpp>

#include "ui_kit.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace driver_form::guide {

namespace ov = swofty::overlay;
using namespace driver_form::ui;

// What a driver is doing right now, from the race's patches.
enum class Moment { none, hot, bad, mistake };

struct Row {
    std::string label;
    std::string tag;    // "HOT STREAK", "SLUMP", "FLYING", "STRUGGLING", "STEADY", "ERRATIC" or ""
    int recent[5]{-1, -1, -1, -1, -1}; // newest first: a position, 0 retired, -1 none, -2 disqualified, -3 not classified
    float form = 0;     // this weekend's form, -1 .. +1
    float pace = 0;     // the AI's target lap time against the game's, seconds (below 0: quicker), as the AI gets it
    std::string role;   // the compact view: "AHEAD", "BEHIND", "TEAMMATE", "HOTTEST", "COLDEST"
    float streak = 0;   // -1 .. +1
    Moment moment = Moment::none;
    bool human = false; // your car: shown, never changed
    bool record = false; // the career has races for this driver
};

struct Data {
    std::vector<Row> rows;     // best form first, yours last
    bool career = false;       // a career: streaks and results; a Grand Prix: the weekend's form only
    std::string level = "normal";      // the Momentum setting now
    std::string level_roll;            // the one this weekend's forms were rolled with ("" unknown)
    std::vector<Row> compact;          // the compact view's rows (up to three)
    std::string session;       // "PRACTICE", "QUALIFYING", "RACE" or ""
    int round = 0;             // the career's round; 0 unknown
    int races = 0;             // races in the career's book
};

struct Key {
    const char *id, *label;
    int vk;
};
constexpr Key kKeys[] = {{"f6", "F6", VK_F6}, {"f5", "F5", VK_F5},    {"f9", "F9", VK_F9},
                         {"f10", "F10", VK_F10}, {"f11", "F11", VK_F11}, {"off", "", 0}};

inline std::mutex g_lock;
inline Data g_data;
inline std::atomic<bool> g_live{false};     // a session with a grid is running
inline std::atomic<int> g_key{0};           // index into kKeys
inline std::atomic<float> g_size{1.0f};     // the "Form Guide size" setting
inline std::atomic<bool> g_hint_on{true};   // the "Form Guide reminder" setting
inline std::atomic<bool> g_hint_new{false}; // the reminder may show (set by the game's thread)
inline std::atomic<bool> g_reduced{false};  // "Form Guide motion: Reduced": no slide, scan line or flashing
inline std::atomic<bool> g_compact_on{true}; // the compact view on track while the drawer is closed
inline int g_page = 0;                      // render thread: the drawer's page when its rows don't fit
inline bool g_open = false;                 // render thread only
inline std::chrono::steady_clock::time_point g_opened, g_hint_from;
inline bool g_hint_shown = false;           // render thread only

inline void set_key(std::string_view id)
{
    for (int i = 0; i < int(std::size(kKeys)); i++)
        if (id == kKeys[i].id)
            g_key = i;
}

inline void set_size(std::string_view id) { g_size = id == "small" ? 0.86f : id == "large" ? 1.16f : 1.0f; }

inline const Key &key() { return kKeys[g_key.load(std::memory_order_relaxed)]; }

// hint: the reminder of the key may show (the first grid of a weekend in this game run)
inline void set(Data data, bool hint = false)
{
    std::lock_guard lock(g_lock);
    g_data = std::move(data);
    g_live = true;
    if (hint)
        g_hint_new = true;
}

inline void clear()
{
    g_live = false;
    std::lock_guard lock(g_lock);
    g_data = Data{};
}

// ---- readings ----

// the heat index: the form as -100 .. +100
inline int heat_index(float form) { return int(std::lround(std::clamp(form, -1.0f, 1.0f) * 100)); }

inline std::string signed_text(int v)
{
    if (v == 0)
        return "0";
    return (v > 0 ? "+" : "\xE2\x88\x92") + std::to_string(std::abs(v)); // a real minus sign
}

// "-0.9s" / "+0.4s"
inline std::string pace_text(float pace)
{
    if (std::fabs(pace) < 0.05f)
        return "0.0s";
    char b[24];
    std::snprintf(b, sizeof b, "%s%.1fs", pace < 0 ? "\xE2\x88\x92" : "+", double(std::fabs(pace)));
    return b;
}

inline uint32_t tag_colour(std::string_view tag)
{
    if (tag == "HOT STREAK")
        return kMagma;
    if (tag == "FLYING")
        return kAmber;
    if (tag == "SLUMP")
        return kIce;
    if (tag == "STRUGGLING")
        return 0x6E8CFFFF;
    if (tag == "ERRATIC")
        return kViolet;
    return kCool; // STEADY
}

// ---- pieces ----

// the thermal scale as a horizontal bar, cold to hot
inline void scale_bar(ov::Paint *p, float x, float y, float w, float h, float a = 1)
{
    float q = w / 4;
    ov::gradient(p, x, y, q, h, alpha(kDeep, a), alpha(kIce, a), true);
    ov::gradient(p, x + q, y, q, h, alpha(kIce, a), alpha(kNeutral, a), true);
    ov::gradient(p, x + 2 * q, y, q, h, alpha(kNeutral, a), alpha(kAmber, a), true);
    ov::gradient(p, x + 3 * q, y, q, h, alpha(kAmber, a), alpha(kMagma, a), true);
}

// a driver's thermometer: the scale, faint, with the reading filled from the middle and a needle (grow: 0..1)
inline void thermometer(ov::Paint *p, float x, float cy, float w, float k, float form, float grow)
{
    float h = 4 * k, centre = x + w / 2;
    scale_bar(p, x, cy - h / 2, w, h, 0.20f);
    ov::rect(p, centre - 0.5f * k, cy - 6 * k, 1 * k, 12 * k, kFaint);
    float f = std::clamp(form, -1.0f, 1.0f) * grow;
    float at = centre + f * (w / 2);
    uint32_t col = thermal(f);
    if (std::fabs(f) > 0.01f) {
        float x0 = std::min(centre, at), len = std::fabs(at - centre);
        ov::gradient(p, x0, cy - h / 2, len, h, f >= 0 ? alpha(col, 0.25f) : col, f >= 0 ? col : alpha(col, 0.25f), true);
    }
    // the needle
    ov::rect(p, at - 1 * k, cy - 7 * k, 2 * k, 14 * k, kText);
    triangle(p, at, cy - 9.5f * k, 3 * k, false, col);
}

// a result as a cell, coloured by how hot it was; the newest first, the older ones fading
inline void result_cell(ov::Paint *p, float x, float cy, float k, int result, float fade)
{
    const float w = 20 * k, h = 18 * k, y = cy - h / 2;
    if (result == -1) {
        ov::rect(p, x + w / 2 - 1 * k, cy - 1 * k, 2 * k, 2 * k, alpha(kFaint, fade)); // no data
        return;
    }
    if (result < -1) { // disqualified, not classified
        ov::rect_outline(p, x, y, w, h, 1 * k, alpha(kViolet, 0.6f * fade));
        ov::text(p, x + w / 2, cy, ts(8.5f * k, alpha(kViolet, fade), ov::align::center | ov::align::middle),
                 result == -2 ? "DQ" : "NC");
        return;
    }
    uint32_t bg = 0, fg = kText2;
    if (result == 0) {
        ov::rect_outline(p, x, y, w, h, 1 * k, alpha(kIce, 0.6f * fade));
        ov::text(p, x + w / 2, cy, ts(10.5f * k, alpha(kIce, fade), ov::align::center | ov::align::middle), "R");
        return;
    }
    if (result == 1)
        bg = kWhiteHot, fg = 0x2A1500FF;
    else if (result == 2)
        bg = 0xFFD08AFF, fg = 0x2A1500FF;
    else if (result == 3)
        bg = kAmber, fg = 0x2A1500FF;
    else if (result <= 10)
        bg = alpha(kAmber, 0.20f), fg = kText;
    else
        bg = 0xFFFFFF0B, fg = kText2;
    ov::rect(p, x, y, w, h, alpha(bg, fade));
    ov::text(p, x + w / 2, cy, ts(10.5f * k, alpha(fg, fade), ov::align::center | ov::align::middle), std::to_string(result));
}

// a driver's trend: their last five results as a line (the top of the box is P1), oldest on the left, the newest a
// dot in the colour of their streak; a retirement is a hollow mark along the bottom
inline void trend(ov::Paint *p, float x, float y, float w, float h, float k, const Row &r)
{
    const int field = 22;
    float px[5], py[5];
    bool have[5] = {};
    int count = 0;
    for (int j = 4; j >= 0; j--) {
        int res = r.recent[j];
        int i = 4 - j; // left to right: oldest to newest
        px[i] = x + w * float(i) / 4;
        if (res > 0) {
            py[i] = y + h * float(std::min(res, field) - 1) / float(field - 1);
            have[i] = true;
            count++;
        } else if (res == 0 || res < -1) {
            ov::rect_outline(p, px[i] - 2 * k, y + h - 2 * k, 4 * k, 4 * k, 1 * k, alpha(res == 0 ? kIce : kViolet, 0.7f));
        }
    }
    if (count == 0) {
        for (float dx = 0; dx < w; dx += 6 * k)
            ov::rect(p, x + dx, y + h / 2, 3 * k, 1 * k, kFaint);
        return;
    }
    uint32_t col = mix(kText2, thermal(r.streak * 1.4f), std::min(1.0f, std::fabs(r.streak) * 2.5f));
    for (int i = 0; i + 1 < 5; i++) {
        if (!have[i])
            continue;
        int nxt = i + 1;
        while (nxt < 5 && !have[nxt])
            nxt++;
        if (nxt < 5) {
            float seg[4] = {px[i], py[i], px[nxt], py[nxt]};
            ov::polyline(p, seg, 2, 1.5f * k, alpha(col, 0.85f));
        }
    }
    for (int i = 0; i < 5; i++)
        if (have[i])
            ov::rect(p, px[i] - 1.2f * k, py[i] - 1.2f * k, 2.4f * k, 2.4f * k, alpha(col, 0.7f));
    for (int i = 4; i >= 0; i--) {
        if (have[i]) {
            ov::circle(p, px[i], py[i], 3.2f * k, thermal(r.streak * 1.4f));
            ov::circle_outline(p, px[i], py[i], 3.2f * k, 1 * k, 0x00000080);
            break;
        }
    }
}

// the live moment instead of the tag: a blinking mark and a word
inline void moment_mark(ov::Paint *p, float x, float cy, float k, Moment m)
{
    uint32_t col = m == Moment::hot ? kMagma : kIce;
    float beat = g_reduced.load() ? 1.0f : m == Moment::mistake ? pulse(0.45f) : pulse(1.1f);
    uint32_t c = alpha(col, 0.55f + 0.45f * beat);
    if (m == Moment::hot)
        triangle(p, x + 4 * k, cy, 4 * k, true, c);
    else if (m == Moment::bad)
        triangle(p, x + 4 * k, cy, 4 * k, false, c);
    else
        diamond(p, x + 4 * k, cy, 4 * k, c);
    const char *word = m == Moment::hot ? "ON A CHARGE" : m == Moment::bad ? "LOSING TIME" : "MISTAKE";
    ov::text(p, x + 12 * k, cy, ts(10 * k, c, ov::align::left | ov::align::middle, 1.2f * k), word);
}

// one of the two readouts under the equaliser: the hottest or the coldest driver
inline void readout(ov::Paint *p, float x, float y, float w, float h, float k, bool hot, const Row *r, bool by_streak)
{
    uint32_t col = hot ? kMagma : kIce;
    brackets(p, x, y, w, h, 7 * k, 1.2f * k, alpha(col, 0.7f));
    ov::rect(p, x, y, w, h, alpha(col, 0.05f));
    // 0.1.0 fix: named for what they measure: the career's streak, or this weekend's form (the table's order)
    const char *label = by_streak ? (hot ? "HOTTEST STREAK" : "COLDEST STREAK") : (hot ? "MOST IN FORM" : "MOST OUT OF FORM");
    ov::text(p, x + 12 * k, y + 13 * k, ts(9.5f * k, col, ov::align::left | ov::align::middle, 2.2f * k), label);
    if (!r) {
        ov::text(p, x + 12 * k, y + h - 15 * k, ts(12.5f * k, kMuted, ov::align::left | ov::align::middle),
                 by_streak ? (hot ? "Nobody is on a hot streak yet" : "Nobody is in a slump yet")
                           : (hot ? "Nobody is in form" : "Nobody is out of form"));
        return;
    }
    ov::TextStyle value = ts(20 * k, col, ov::align::right | ov::align::middle, 0.5f * k);
    std::string v = by_streak ? signed_text(heat_index(r->streak)) : signed_text(heat_index(r->form));
    float vw = text_w(p, value, v);
    ov::text(p, x + w - 12 * k, y + h - 16 * k, value, v);
    ov::TextStyle ns = ts(15 * k, kText, ov::align::left | ov::align::middle, 0.8f * k);
    float room = w - 24 * k - vw - 10 * k;
    std::string tail;
    if (by_streak) {
        for (int j = 2; j >= 0; j--) // oldest first, the newest last: the table's order
            if (r->recent[j] != -1)
                tail += (tail.empty() ? "" : " ") + (r->recent[j] == 0    ? std::string("R")
                                                     : r->recent[j] == -2 ? std::string("DQ")
                                                     : r->recent[j] == -3 ? std::string("NC")
                                                                          : "P" + std::to_string(r->recent[j]));
    }
    ov::TextStyle tsml = ts(10.5f * k, kText2, ov::align::left | ov::align::middle, 0.6f * k);
    float tail_w = tail.empty() ? 0 : text_w(p, tsml, tail) + 10 * k;
    std::string name = fit(p, ns, r->label, std::max(40 * k, room - tail_w));
    ov::text(p, x + 12 * k, y + h - 15 * k, ns, name);
    if (!tail.empty())
        ov::text(p, x + 12 * k + text_w(p, ns, name) + 10 * k, y + h - 15 * k, tsml, tail);
}

// ---- the drawer ----

// t: seconds since it opened (the slide and the scan). Also drawn by the preview (tests\form_guide_preview.cpp).
inline void draw(ov::Paint *p, const Data &d_in, float t = 10)
{
    if (g_reduced.load())
        t = 10; // "Form Guide motion: Reduced": drawn as it ends up, at once
    // never below 0.8 of a 1080p screen's scale: at 720p the names stay about 11 px and the rows about 19 px (the
    // short screen's tight layout and the pages take what doesn't fit)
    float k = std::max(ov::ui_scale(p) * g_size.load(), 0.8f);
    float sw = 0, sh = 0;
    ov::size(p, &sw, &sh);
    ov::Rect safe = ov::safe_area(p);
    // 0.1.0 fix: a short screen (720p, or Large at 1080p) drops the equaliser and the thermometer column and keeps
    // three results: the rows get the room
    bool tight = safe.h / k < 900;
    // the history (streak and results) only once the career has a race: before that, one "no history yet" note and
    // the drawer narrower by the empty columns
    const bool history = d_in.career && d_in.races > 0;
    const float padl = 28 * k, head_h = 100 * k, band_h = (tight ? 64 : 122) * k, cols_h = 40 * k, foot_h = 46 * k;
    // the drawer docks to the screen's right edge; its text stays inside the safe area. On an ultrawide screen whose
    // safe area is narrower than the screen, it docks to the safe area's edge
    float edge = sw - (safe.x + safe.w);
    bool pillar = edge > 120 * k;
    float dock = pillar ? safe.x + safe.w : sw;
    const float padr = 22 * k + (pillar ? 0 : edge);
    const float heat_w = tight ? 0 : 106 * k;
    const int cells = tight ? 3 : 5;
    const float o_tag = 163 * k, o_heat = o_tag + 100 * k, o_index = o_heat + heat_w + 40 * k, o_lap = o_index + 64 * k;
    const float o_trend = o_lap + 26 * k, o_last = o_trend + 56 * k + 14 * k;
    // never narrower than its header needs (a first race at 720p has no history and no thermometer)
    const float w = std::max(padl + (history ? o_last + float(cells) * 22 * k : o_lap) + padr, 440 * k + (padr - 22 * k));
    // the rows: all of them when they fit at 24 px (scaled), a height that still reads; else a page at a time, your
    // car always at the bottom
    constexpr float kMinRow = 24;
    float fixed = head_h + band_h + cols_h + foot_h + 8 * k;
    Data d = d_in;
    int pages = 1;
    size_t fit_rows = size_t(std::max(2.0f, std::floor((safe.h - fixed) / (kMinRow * k))));
    if (d.rows.size() > fit_rows) {
        std::vector<Row> ai, mine;
        for (const Row &r : d_in.rows)
            (r.human ? mine : ai).push_back(r);
        size_t per = std::max<size_t>(1, fit_rows - mine.size());
        pages = int((ai.size() + per - 1) / per);
        g_page = std::clamp(g_page, 0, pages - 1);
        d.rows.assign(ai.begin() + std::min(ai.size(), size_t(g_page) * per),
                      ai.begin() + std::min(ai.size(), size_t(g_page + 1) * per));
        d.rows.insert(d.rows.end(), mine.begin(), mine.end());
    } else {
        g_page = 0;
    }
    float row_h = 30 * k;
    if (fixed + row_h * float(d.rows.size()) > safe.h)
        row_h = std::max(kMinRow * k, (safe.h - fixed) / std::max<float>(1, float(d.rows.size())));
    float h = std::min(safe.h, fixed + row_h * float(d.rows.size()));
    float slide = ease_out(t / 0.32f);
    float x = dock - w * slide, y = safe.y + std::max(0.0f, (safe.h - h) * 0.42f);
    const float cham = 26 * k;

    // the drawer: a soft shadow to its left, the body with its top-left corner cut, a faint grid and the thermal edge
    ov::gradient(p, x - 28 * k, y, 28 * k, h, 0x00000000, 0x00000048, true);
    shape(p, {x + cham, y, x + w, y, x + w, y + h, x, y + h, x, y + cham}, alpha(kBase, 0.95f));
    {
        ov::ClipScope clip(p, x + 4 * k, y, w - 4 * k, h);
        // the grid, fainter than before: the names come first
        for (float gx = x + 44 * k; gx < x + w; gx += 44 * k)
            ov::rect(p, gx, y, 1, h, kGridSoft);
        for (float gy = y + 44 * k; gy < y + h; gy += 44 * k)
            ov::rect(p, x, gy, w, 1, kGridSoft);
    }
    ov::gradient(p, x, y + cham, 3 * k, h - cham, kMagma, kDeep, false);
    shape(p, {x, y + cham, x + cham, y, x + cham + 3 * k, y, x + 3 * k, y + cham}, kMagma);
    ov::gradient(p, x + cham, y, w * 0.45f, 1.5f * k, alpha(kMagma, 0.8f), alpha(kMagma, 0.0f), true);

    // the header: the addon's name, where the session stands, the title and the scale it is read on
    float lx = x + padl, rx = x + w - padr;
    ov::TextStyle brand = ts(10 * k, kMuted, ov::align::left | ov::align::middle, 3.4f * k);
    float brand_w = text_w(p, brand, "DRIVER MOMENTUM");
    {
        std::string crumbs;
        auto add = [&](std::string part) {
            for (char &c : part)
                c = char(std::toupper(static_cast<unsigned char>(c)));
            crumbs += (crumbs.empty() ? "" : "   /   ") + part;
        };
        if (d.round > 0)
            add("ROUND " + std::to_string(d.round));
        if (!d.session.empty())
            add(d.session);
        if (d.career) {
            // 0.1.0 fix: the weekend's forms keep the level they were rolled with; a new setting starts next weekend
            if (!d.level_roll.empty() && d.level_roll != d.level)
                add("MOMENTUM " + d.level_roll + " \xC2\xB7 " + d.level + " NEXT WEEKEND");
            else
                add("MOMENTUM " + d.level);
        }
        if (!d.career)
            add("GRAND PRIX");
        // the session's place on the right; the addon's name on the left only when both fit
        ov::TextStyle cs = ts(10 * k, kText2, ov::align::right | ov::align::middle, 1.6f * k);
        float crumbs_w = text_w(p, cs, crumbs);
        if (brand_w + 20 * k + crumbs_w <= rx - lx)
            ov::text(p, lx, y + 22 * k, brand, "DRIVER MOMENTUM");
        ov::text(p, rx, y + 22 * k, cs, fit(p, cs, crumbs, rx - lx));
    }
    ov::TextStyle title = ts(31 * k, kText, ov::align::left | ov::align::top, 5.5f * k);
    ov::text(p, lx - 1 * k, y + 37 * k, title, "FORM GUIDE");
    {
        // the scale gives way to the title on a narrow drawer
        float bw = std::clamp(rx - lx - text_w(p, title, "FORM GUIDE") - 24 * k, 96 * k, 168 * k);
        float bx = rx - bw, by = y + 54 * k;
        scale_bar(p, bx, by, bw, 5 * k);
        for (int i = 0; i <= 4; i++)
            ov::rect(p, bx + bw * float(i) / 4 - 0.5f * k, by + 7 * k, 1 * k, i % 2 == 0 ? 5 * k : 3 * k, kMuted);
        ov::text(p, bx, by + 15 * k, ts(9 * k, kIce, ov::align::left | ov::align::top, 2.0f * k), "COLD");
        ov::text(p, bx + bw / 2, by + 15 * k, ts(9 * k, kMuted, ov::align::center | ov::align::top, 2.0f * k), "0");
        ov::text(p, bx + bw, by + 15 * k, ts(9 * k, kMagma, ov::align::right | ov::align::top, 2.0f * k), "HOT");
    }
    ov::rect(p, lx, y + head_h - 1 * k, rx - lx, 1 * k, kRule);

    // the band: the whole grid's heat as an equaliser, then the hottest and the coldest driver
    float by = y + head_h + 12 * k;
    float grow = ease_out((t - 0.18f) / 0.5f);
    {
        std::vector<const Row *> ai;
        for (const Row &r : d_in.rows) // the whole grid, whatever page the rows are on
            if (!r.human)
                ai.push_back(&r);
        float area = rx - lx, gap = 2 * k, mid = by + 20 * k;
        float bw = ai.empty() ? 0 : (area - gap * float(ai.size() - 1)) / float(ai.size());
        if (!tight)
            ov::rect(p, lx, mid - 0.5f * k, area, 1 * k, kFaint);
        for (size_t i = 0; i < ai.size() && !tight; i++) {
            float f = ai[i]->form;
            float bh = std::max(1.5f * k, std::fabs(f) * 18 * k * grow);
            float bx = lx + float(i) * (bw + gap);
            uint32_t col = thermal(f);
            if (f >= 0) {
                ov::gradient(p, bx, mid - bh, bw, bh, col, alpha(col, 0.35f), false);
                ov::rect(p, bx, mid - bh, bw, 1.5f * k, mix(col, kWhiteHot, 0.4f));
            } else {
                ov::gradient(p, bx, mid, bw, bh, alpha(col, 0.35f), col, false);
                ov::rect(p, bx, mid + bh - 1.5f * k, bw, 1.5f * k, mix(col, 0xFFFFFFFF, 0.3f));
            }
        }
        const Row *hot = nullptr, *cold = nullptr;
        bool by_streak = d.career && d.races > 0;
        for (const Row *r : ai) {
            if (by_streak) {
                if (!r->record)
                    continue;
                if (r->streak >= 0.15f && (!hot || r->streak > hot->streak))
                    hot = r;
                if (r->streak <= -0.15f && (!cold || r->streak < cold->streak))
                    cold = r;
            } else {
                if (r->form > 0.05f && (!hot || r->form > hot->form))
                    hot = r;
                if (r->form < -0.05f && (!cold || r->form < cold->form))
                    cold = r;
            }
        }
        float ry = by + (tight ? 0 : 48) * k, rw = (area - 12 * k) / 2, rh = 48 * k;
        float ra = ease_out((t - 0.25f) / 0.4f);
        ov::OpacityScope fade(p, ra);
        readout(p, lx, ry, rw, rh, k, true, hot, by_streak);
        readout(p, lx + rw + 12 * k, ry, rw, rh, k, false, cold, by_streak);
    }

    // the columns
    float top = y + head_h + band_h + cols_h;
    const float c_pos = lx + 14 * k, c_stripe = lx + 21 * k, c_name = lx + 31 * k, c_tag = lx + o_tag;
    const float c_heat = lx + o_heat, c_index = lx + o_index;
    const float c_lap = lx + o_lap, c_trend = lx + o_trend, trend_w = 56 * k, c_last = lx + o_last;
    const float last_end = c_last + float(cells) * 22 * k - 2 * k; // the right edge of the newest result
    {
        // two groups: this weekend (the form and what it does to the AI's lap) and the history (the streak and the
        // results), each named over its columns with a line in its end of the scale
        float gy = top - cols_h + 9 * k, cy = top - 11 * k;
        ov::TextStyle grp = ts(9 * k, kText2, ov::align::left | ov::align::middle, 1.8f * k);
        ov::TextStyle cap = ts(9.5f * k, kHead, ov::align::left | ov::align::middle, 1.2f * k);
        ov::TextStyle capr = ts(9.5f * k, kHead, ov::align::right | ov::align::middle, 1.2f * k);
        float wk0 = tight ? c_index - 44 * k : c_heat;
        ov::text(p, wk0, gy, grp, "THIS WEEKEND");
        ov::gradient(p, wk0, gy + 7 * k, c_lap - wk0, 1 * k, alpha(kAmber, 0.55f), alpha(kMagma, 0.15f), true);
        if (history) {
            ov::text(p, c_trend, gy, grp, "HISTORY");
            ov::gradient(p, c_trend, gy + 7 * k, last_end - c_trend, 1 * k, alpha(kIce, 0.15f), alpha(kIce, 0.55f), true);
        }
        ov::text(p, c_pos, cy, capr, "#");
        ov::text(p, c_name, cy, cap, "DRIVER");
        if (!tight)
            ov::text(p, c_heat, cy, cap, "THERMOMETER");
        ov::text(p, c_index, cy, capr, "FORM");
        ov::text(p, c_lap, cy, capr, "AI TARGET");
        if (history) {
            ov::text(p, c_trend, cy, cap, "STREAK");
            ov::text(p, c_last, cy, cap, cells == 5 ? "LAST 5" : "LAST 3");
            // the newest result is the last cell, as the streak's line ends on the newest: marked over its column
            ov::text(p, last_end, gy, ts(9 * k, kText, ov::align::right | ov::align::middle, 1.2f * k), "LATEST");
            triangle(p, last_end - 10 * k, cy + 1 * k, 3 * k, false, kText);
        }
        ov::rect(p, lx, top - 1 * k, rx - lx, 1 * k, kRule);
    }

    // the rows, revealed by a scan line moving down
    float bottom = y + h - foot_h;
    float scan_t = (t - 0.22f) / 0.6f;
    float scan = top + (bottom - top + row_h) * ease_in_out(scan_t);
    {
        ov::ClipScope clip(p, x + 4 * k, top, w - 4 * k, bottom - top);
        ov::TextStyle name_s = ts(14 * k, kText, ov::align::left | ov::align::middle, 0.6f * k);
        // the history's columns sit on their own faint band, the newest result's column a touch lighter, with a line
        // between this weekend and the history
        if (history) {
            ov::rect(p, c_trend - 12 * k, top, rx - (c_trend - 12 * k), bottom - top, 0xFFFFFF03);
            ov::rect(p, last_end - 21 * k, top, 22 * k, bottom - top, 0xFFFFFF06);
            ov::rect(p, c_trend - 13 * k, top + 4 * k, 1 * k, bottom - top - 8 * k, kRule);
        }
        int rank = 0;
        for (size_t i = 0; i < d.rows.size(); i++) {
            const Row &r = d.rows[i];
            float ry = top + float(i) * row_h, mid = ry + row_h / 2;
            if (ry > bottom)
                break;
            float a = std::clamp((scan - ry) / (row_h * 1.6f), 0.0f, 1.0f);
            if (a <= 0)
                continue;
            ov::OpacityScope row_fade(p, a);
            if (!r.human)
                ++rank;
            // the row's band: striped, yours tinted, a live moment flashing
            if (r.human)
                ov::rect(p, x + 4 * k, ry, w - 4 * k, row_h, alpha(kYou, 0.08f));
            else if (r.moment != Moment::none)
                ov::rect(p, x + 4 * k, ry, w - 4 * k, row_h,
                         alpha(r.moment == Moment::hot ? kMagma : kIce, 0.06f + 0.06f * (g_reduced.load() ? 0.5f : pulse(0.9f))));
            else if (i % 2 == 1)
                ov::rect(p, x + 4 * k, ry, w - 4 * k, row_h, 0xFFFFFF04);
            // position and the heat stripe
            if (!r.human)
                ov::text(p, c_pos, mid, ts(12 * k, rank <= 3 ? kText : kMuted, ov::align::right | ov::align::middle), std::to_string(rank));
            ov::rect(p, c_stripe, ry + 6 * k, 3 * k, row_h - 12 * k, r.human ? kYou : thermal(r.form));
            // the name, and the tag or the live moment
            ov::TextStyle ns = name_s;
            if (r.human)
                ns.rgba = kText2;
            ov::text(p, c_name, mid, ns, fit(p, ns, r.label, c_tag - c_name - 8 * k));
            if (r.human) {
                ov::rect(p, c_tag, mid - 2.5f * k, 5 * k, 5 * k, kYou);
                ov::text(p, c_tag + 10 * k, mid, ts(10 * k, kYou, ov::align::left | ov::align::middle, 1.4f * k), "YOU");
            } else if (r.moment != Moment::none) {
                moment_mark(p, c_tag, mid, k, r.moment);
            } else if (!r.tag.empty()) {
                uint32_t tc = tag_colour(r.tag);
                ov::rect(p, c_tag, mid - 2.5f * k, 5 * k, 5 * k, tc);
                ov::text(p, c_tag + 10 * k, mid, ts(10 * k, tc, ov::align::left | ov::align::middle, 1.4f * k), r.tag);
            }
            // the thermometer, the heat index and the time a lap
            if (r.human) {
                ov::text(p, tight ? c_lap : c_heat + heat_w / 2, mid,
                         ts(9.5f * k, kMuted, (tight ? ov::align::right : ov::align::center) | ov::align::middle, 2.0f * k),
                         "NOT CHANGED");
            } else {
                float g = std::clamp((scan - ry) / (row_h * 4), 0.0f, 1.0f);
                if (!tight)
                    thermometer(p, c_heat, mid, heat_w, k, r.form, ease_out(g));
                int idx = heat_index(r.form);
                ov::text(p, c_index, mid, ts(14 * k, thermal(r.form), ov::align::right | ov::align::middle, 0.3f * k),
                         signed_text(idx));
                ov::text(p, c_lap, mid, ts(11.5f * k, std::fabs(r.pace) < 0.05f ? kMuted : thermal(-r.pace / 1.5f),
                                          ov::align::right | ov::align::middle, 0.3f * k),
                         pace_text(r.pace));
            }
            if (!history)
                continue;
            trend(p, c_trend, ry + 6 * k, trend_w, row_h - 12 * k, k, r);
            // oldest on the left, the newest on the right (under LATEST), the same way the streak's line runs
            for (int i = 0; i < cells; i++) {
                int j = cells - 1 - i; // recent[0] is the newest
                result_cell(p, c_last + float(i) * 22 * k, mid, k, r.recent[j], 1.0f - 0.13f * float(j));
            }
        }
        // the scan line itself, while it moves
        if (scan_t > 0 && scan_t < 1) {
            ov::gradient(p, x + 4 * k, scan - 26 * k, w - 4 * k, 26 * k, alpha(kMagma, 0.0f), alpha(kMagma, 0.10f), false);
            float q = (w - 4 * k) / 4;
            ov::gradient(p, x + 4 * k, scan - 1 * k, q * 2, 2 * k, alpha(kIce, 0.2f), kIce, true);
            ov::gradient(p, x + 4 * k + q * 2, scan - 1 * k, q * 2, 2 * k, kAmber, alpha(kMagma, 0.2f), true);
        }
    }

    // the footer: the key, how to read the heat, and the book
    {
        float fy = y + h - foot_h / 2;
        ov::rect(p, lx, y + h - foot_h, rx - lx, 1 * k, kRule);
        float fx = lx;
        if (key().vk) {
            float kw = keycap(p, fx, fy - 11 * k, key().label, k);
            ov::text(p, fx + kw + 9 * k, fy, ts(10 * k, kText2, ov::align::left | ov::align::middle, 2.0f * k), "CLOSE");
            fx += kw + 9 * k + text_w(p, ts(10 * k, kText2, ov::align::left | ov::align::middle, 2.0f * k), "CLOSE") + 24 * k;
        }
        ov::TextStyle leg = ts(9.5f * k, kMuted, ov::align::left | ov::align::middle, 1.4f * k);
        if (pages > 1) { // more rows than fit: both page keys and where you are
            float pw = keycap(p, fx, fy - 11 * k, "PgUp", k, g_page > 0 ? kText : kMuted);
            fx += pw + 4 * k;
            pw = keycap(p, fx, fy - 11 * k, "PgDn", k, g_page + 1 < pages ? kText : kMuted);
            std::string more = "PAGE " + std::to_string(g_page + 1) + " / " + std::to_string(pages);
            ov::TextStyle ps = ts(10 * k, kText2, ov::align::left | ov::align::middle, 1.2f * k);
            ov::text(p, fx + pw + 8 * k, fy, ps, more);
            fx += pw + 8 * k + text_w(p, ps, more) + 20 * k;
        }
        // the history's one note: none yet in a new career, none at all in a Grand Prix. It comes first; the form's
        // legend (the header has the scale too) only when there is room for both
        bool first = d.career && d.races == 0;
        std::string right = !d.career ? "NO MEMORY IN A GRAND PRIX"
                            : first   ? "NO HISTORY YET \xC2\xB7 IT STARTS AFTER THE FIRST RACE"
                                      : std::to_string(d.races) + (d.races == 1 ? " RACE IN THIS CAREER" : " RACES IN THIS CAREER");
        std::string right_short = first ? "NO HISTORY YET" : right;
        ov::TextStyle rs = ts(9.5f * k, first ? kText2 : kMuted, ov::align::right | ov::align::middle, 1.2f * k);
        float legend_w = text_w(p, leg, "FORM") + 8 * k + 90 * k + text_w(p, leg, "+100");
        if (fx + legend_w + 16 * k + text_w(p, rs, right_short) <= rx) {
            ov::text(p, fx, fy, leg, "FORM");
            fx += text_w(p, leg, "FORM") + 8 * k;
            ov::text(p, fx, fy, ts(9.5f * k, kIce, ov::align::left | ov::align::middle, 1.0f * k), "\xE2\x88\x92" "100");
            fx += 30 * k;
            scale_bar(p, fx, fy - 2 * k, 54 * k, 4 * k);
            fx += 60 * k;
            ov::text(p, fx, fy, ts(9.5f * k, kMagma, ov::align::left | ov::align::middle, 1.0f * k), "+100");
            fx += text_w(p, leg, "+100");
        }
        if (text_w(p, rs, right) > rx - fx - 16 * k)
            right = right_short;
        ov::text(p, rx, fy, rs, fit(p, rs, right, rx - fx - 10 * k));
    }
}

// The reminder: a tab that slides out of the screen's right edge for a few seconds; t is the seconds since it began.
inline void draw_hint(ov::Paint *p, const Data &d, float t)
{
    constexpr float kShow = 7.0f;
    if (t < 0 || t > kShow || !key().vk)
        return;
    float k = std::max(ov::ui_scale(p) * g_size.load(), 0.8f);
    float sw = 0, sh = 0;
    ov::size(p, &sw, &sh);
    ov::Rect safe = ov::safe_area(p);
    float out = g_reduced.load() ? 1.0f : ease_out(t / 0.4f) * (1 - ease_out((t - (kShow - 0.5f)) / 0.5f));
    int hot = 0, cold = 0;
    for (const Row &r : d.rows) {
        hot += !r.human && r.record && r.streak >= 0.55f ? 1 : 0;
        cold += !r.human && r.record && r.streak <= -0.55f ? 1 : 0;
    }
    std::string sub = d.career && (hot || cold)
                          ? std::to_string(hot) + " hot, " + std::to_string(cold) + " cold on the grid"
                          : "Who is hot and who is cold this weekend";
    ov::TextStyle title = ts(12 * k, kText, ov::align::left | ov::align::middle, 3.0f * k);
    ov::TextStyle subs = ts(11 * k, kText2, ov::align::left | ov::align::middle, 0.4f * k);
    float kw = std::max(28 * k, text_w(p, ts(12 * k, kText, ov::align::center | ov::align::middle, 1.0f * k), key().label) + 14 * k);
    float w = 22 * k + kw + 14 * k + std::max(text_w(p, title, "FORM GUIDE"), text_w(p, subs, sub)) + 26 * k;
    float h = 54 * k;
    float x = sw - w * out, y = safe.y + safe.h * 0.66f;
    ov::gradient(p, x - 20 * k, y, 20 * k, h, 0x00000000, 0x00000040, true);
    ov::rect(p, x, y, w, h, alpha(kBase, 0.95f));
    ov::gradient(p, x, y, 3 * k, h, kMagma, kDeep, false);
    keycap(p, x + 22 * k, y + h / 2 - 11 * k, key().label, k);
    ov::text(p, x + 22 * k + kw + 14 * k, y + 19 * k, title, "FORM GUIDE");
    ov::text(p, x + 22 * k + kw + 14 * k, y + 36 * k, subs, sub);
    // the time left, as the thermal scale burning down along the bottom
    float left = std::clamp(1 - t / kShow, 0.0f, 1.0f);
    ov::ClipScope clip(p, x + 3 * k, y + h - 2 * k, (w - 3 * k) * left, 2 * k);
    scale_bar(p, x + 3 * k, y + h - 2 * k, w - 3 * k, 2 * k);
}

// 0.1.0: the compact view on track while the drawer is closed: up to three drivers that matter now (in a race the
// cars ahead and behind you and your teammate; otherwise the most and least in form and your teammate), each with
// this weekend's form and what they're doing right now. It never takes the keys or the mouse.
// the compact view's role, short: the name gets the room
inline const char *short_role(std::string_view role)
{
    if (role == "TEAMMATE")
        return "MATE";
    if (role == "HOTTEST")
        return "HOT";
    if (role == "COLDEST")
        return "COLD";
    if (role == "BEHIND")
        return "BEHIND";
    if (role == "AHEAD")
        return "AHEAD";
    return role.empty() ? "" : role.data();
}

inline void draw_compact(ov::Paint *p, const Data &d)
{
    if (d.compact.empty())
        return;
    // never below 0.95 of a 1080p screen's scale: at 720p its names stay about 13 px, its labels about 9.5 px
    float k = std::max(ov::ui_scale(p) * g_size.load() * 0.92f, 0.95f);
    float sw = 0, sh = 0;
    ov::size(p, &sw, &sh);
    ov::Rect safe = ov::safe_area(p);
    float edge = sw - (safe.x + safe.w);
    float dock = edge > 120 * k ? safe.x + safe.w : sw;
    float row_h = 28 * k, head = 22 * k;
    float w = 256 * k + (edge > 120 * k ? 0 : edge); // the strip reaches the screen's edge, its text stays inside
    float h = head + row_h * float(d.compact.size()) + 6 * k;
    float x = dock - w, y = safe.y + safe.h * 0.24f;
    float padr = 12 * k + (edge > 120 * k ? 0 : edge);
    ov::gradient(p, x - 16 * k, y, 16 * k, h, 0x00000000, 0x00000038, true);
    float cham = 10 * k;
    shape(p, {x + cham, y, x + w, y, x + w, y + h, x, y + h, x, y + cham}, alpha(kBase, 0.92f));
    ov::gradient(p, x, y + cham, 2 * k, h - cham, kMagma, kDeep, false);
    float lx = x + 12 * k, rx = x + w - padr;
    ov::text(p, lx, y + head / 2 + 1 * k, ts(9.5f * k, kText2, ov::align::left | ov::align::middle, 1.6f * k), "FORM GUIDE");
    if (key().vk)
        ov::text(p, rx, y + head / 2 + 1 * k, ts(9.5f * k, kText2, ov::align::right | ov::align::middle, 1.0f * k),
                 std::string(key().label) + " FULL");
    ov::rect(p, lx, y + head, rx - lx, 1 * k, kRule);
    float ry = y + head + 3 * k;
    // the role in a narrow column, the name with the room, then the value with the state's mark right beside it
    const float role_w = 48 * k, name_x = lx + 10 * k + role_w;
    ov::TextStyle vs = ts(14 * k, kText, ov::align::right | ov::align::middle, 0.2f * k);
    const float value_w = text_w(p, vs, "+100");
    for (const Row &r : d.compact) {
        float mid = ry + row_h / 2;
        ov::rect(p, lx, ry + 6 * k, 3 * k, row_h - 12 * k, r.human ? kYou : thermal(r.form));
        ov::text(p, lx + 10 * k, mid, ts(10 * k, kText2, ov::align::left | ov::align::middle, 0.8f * k), short_role(r.role));
        float value_left;
        if (!r.human) {
            vs.rgba = thermal(r.form);
            std::string v = signed_text(heat_index(r.form));
            ov::text(p, rx, mid, vs, v);
            value_left = rx - text_w(p, vs, v);
        } else {
            ov::TextStyle ys = ts(10.5f * k, kYou, ov::align::right | ov::align::middle, 1.0f * k);
            ov::text(p, rx, mid, ys, "YOU");
            value_left = rx - text_w(p, ys, "YOU");
        }
        if (r.moment != Moment::none) {
            uint32_t col = r.moment == Moment::hot ? kMagma : kIce;
            float mx = value_left - 9 * k;
            if (r.moment == Moment::hot)
                triangle(p, mx, mid, 4 * k, true, col);
            else if (r.moment == Moment::bad)
                triangle(p, mx, mid, 4 * k, false, col);
            else
                diamond(p, mx, mid, 4 * k, col);
        }
        ov::TextStyle ns = ts(14 * k, r.human ? kText2 : kText, ov::align::left | ov::align::middle, 0.3f * k);
        float name_room = rx - value_w - 18 * k - name_x;
        ov::text(p, name_x, mid, ns, fit(p, ns, r.label, name_room));
        ry += row_h;
    }
}

// The overlay's paint callback (render thread).
inline void paint(ov::Paint *p, void *)
{
    if (!g_live.load()) {
        g_open = false;
        g_hint_shown = false;
        return;
    }
    auto now = std::chrono::steady_clock::now();
    if (g_hint_new.exchange(false) && g_hint_on.load()) {
        g_hint_from = now;
        g_hint_shown = true;
    }
    const Key &k = key();
    ov::Input in = ov::input(p);
    if (k.vk) {
        if (in.key_pressed(uint32_t(k.vk))) {
            g_open = !g_open;
            g_opened = now;
            g_hint_shown = false; // found it
            g_page = 0;
            swofty::log("[driver-momentum] Form Guide %s", g_open ? "opened" : "closed");
        }
    }
    // the page keys, read like the key above (the game keeps them): only while the drawer is open
    if (g_open && in.key_pressed(0x22)) // Page Down
        ++g_page;
    if (g_open && in.key_pressed(0x21) && g_page > 0) // Page Up
        --g_page;
    bool compact = !g_open && g_compact_on.load();
    if (!g_open && !g_hint_shown && !compact)
        return;
    Data data;
    {
        std::lock_guard lock(g_lock);
        data = g_data;
    }
    if (data.rows.empty())
        return;
    if (g_open) {
        draw(p, data, std::chrono::duration<float>(now - g_opened).count());
        return;
    }
    if (compact)
        draw_compact(p, data);
    if (!g_hint_shown)
        return;
    float t = std::chrono::duration<float>(now - g_hint_from).count();
    if (t > 8)
        g_hint_shown = false;
    else
        draw_hint(p, data, t);
}

} // namespace driver_form::guide
