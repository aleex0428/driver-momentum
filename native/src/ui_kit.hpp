// SPDX-License-Identifier: MIT
// Driver Momentum's drawing kit: the thermal palette (ice for a cold driver, magma for a hot one), colour and text
// helpers, and the crisp, square shapes the Form Guide is built from. Vector calls only; no textures.
#pragma once
#include <swofty/overlay.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace driver_form::ui {

namespace ov = swofty::overlay;

// ---- palette (RGBA) ----
constexpr uint32_t kBase = 0x0A0C10FF;    // the drawer
constexpr uint32_t kRaised = 0x12161CFF;  // a band on it
constexpr uint32_t kText = 0xE9EEF2FF, kText2 = 0x9AA5B1FF, kMuted = 0x5F6A76FF, kFaint = 0x39424CFF;
constexpr uint32_t kGrid = 0xFFFFFF07, kRule = 0xFFFFFF16;
constexpr uint32_t kGridSoft = 0xFFFFFF04, kHead = 0xB4BEC9FF; // the drawer's faint grid; column heads, brighter
constexpr uint32_t kDeep = 0x2D5BFFFF, kIce = 0x46D2FFFF, kCool = 0x7FA6C2FF, kNeutral = 0x56606CFF;
constexpr uint32_t kAmber = 0xFFB547FF, kMagma = 0xFF5A1FFF, kWhiteHot = 0xFFF0D2FF;
constexpr uint32_t kViolet = 0xB98CFFFF, kYou = 0x8FA8FFFF;

inline uint32_t alpha(uint32_t rgba, float a)
{
    uint32_t al = static_cast<uint32_t>(std::clamp(a * static_cast<float>(rgba & 0xFF), 0.0f, 255.0f));
    return (rgba & 0xFFFFFF00u) | al;
}

// the colour between two by t (0..1), alpha included
inline uint32_t mix(uint32_t a, uint32_t b, float t)
{
    t = std::clamp(t, 0.0f, 1.0f);
    uint32_t out = 0;
    for (int s = 0; s < 32; s += 8) {
        float va = static_cast<float>((a >> s) & 0xFF), vb = static_cast<float>((b >> s) & 0xFF);
        out |= static_cast<uint32_t>(va + (vb - va) * t + 0.5f) << s;
    }
    return out;
}

// The thermal scale: -1 deep blue, -0.5 ice, 0 graphite, +0.5 amber, +1 magma.
inline uint32_t thermal(float v)
{
    v = std::clamp(v, -1.0f, 1.0f);
    if (v < -0.5f)
        return mix(kDeep, kIce, (v + 1.0f) * 2.0f);
    if (v < 0.0f)
        return mix(kIce, kNeutral, (v + 0.5f) * 2.0f);
    if (v < 0.5f)
        return mix(kNeutral, kAmber, v * 2.0f);
    return mix(kAmber, kMagma, (v - 0.5f) * 2.0f);
}

// ---- time ----
inline float clock_s()
{
    static const auto start = std::chrono::steady_clock::now();
    return std::chrono::duration<float>(std::chrono::steady_clock::now() - start).count();
}

// 0..1..0 once per period
inline float pulse(float period, float phase = 0)
{
    return 0.5f - 0.5f * std::cos((clock_s() / period + phase) * 6.2831853f);
}

inline float ease_out(float t)
{
    t = std::clamp(t, 0.0f, 1.0f);
    return 1 - (1 - t) * (1 - t) * (1 - t);
}

inline float ease_in_out(float t)
{
    t = std::clamp(t, 0.0f, 1.0f);
    return t < 0.5f ? 4 * t * t * t : 1 - std::pow(-2 * t + 2, 3.0f) / 2;
}

// ---- text ----
inline ov::TextStyle ts(float size, uint32_t rgba = kText, uint32_t align = ov::align::left | ov::align::top,
                        float tracking = 0)
{
    ov::TextStyle s;
    s.size = size;
    s.rgba = rgba;
    s.align = align;
    s.tracking = tracking;
    return s;
}

// text bounds, remembered: the same strings are measured every frame. Paint thread only.
inline float text_w(ov::Paint *p, const ov::TextStyle &s, std::string_view t)
{
    static std::unordered_map<std::string, float> cache;
    char head[48];
    std::snprintf(head, sizeof head, "%.2f|%.2f|", s.size, s.tracking);
    std::string key(head);
    key.append(t);
    if (auto it = cache.find(key); it != cache.end())
        return it->second;
    float w = 0, h = 0;
    ov::measure(p, s, t, &w, &h);
    if (cache.size() > 4000)
        cache.clear();
    cache.emplace(std::move(key), w);
    return w;
}

// text cut to a width with "..."
inline std::string fit(ov::Paint *p, const ov::TextStyle &s, std::string t, float max_w)
{
    if (text_w(p, s, t) <= max_w)
        return t;
    while (!t.empty() && text_w(p, s, t + "...") > max_w) {
        t.pop_back();
        while (!t.empty() && (static_cast<unsigned char>(t.back()) & 0xC0) == 0x80)
            t.pop_back();
    }
    return t + "...";
}

// ---- shapes ----

// a filled polygon from points
inline void shape(ov::Paint *p, std::initializer_list<float> xy, uint32_t col)
{
    float pts[16];
    uint32_t n = 0;
    for (float v : xy)
        if (n < 16)
            pts[n++] = v;
    ov::polygon(p, pts, n / 2, col);
}

inline void triangle(ov::Paint *p, float cx, float cy, float s, bool up, uint32_t col)
{
    if (up)
        shape(p, {cx - s, cy + s * 0.55f, cx + s, cy + s * 0.55f, cx, cy - s * 0.75f}, col);
    else
        shape(p, {cx - s, cy - s * 0.55f, cx + s, cy - s * 0.55f, cx, cy + s * 0.75f}, col);
}

inline void diamond(ov::Paint *p, float cx, float cy, float r, uint32_t col)
{
    shape(p, {cx, cy - r, cx + r, cy, cx, cy + r, cx - r, cy}, col);
}

// corner brackets around a box: the frame of the drawer's readouts
inline void brackets(ov::Paint *p, float x, float y, float w, float h, float len, float t, uint32_t col)
{
    ov::rect(p, x, y, len, t, col);
    ov::rect(p, x, y, t, len, col);
    ov::rect(p, x + w - len, y, len, t, col);
    ov::rect(p, x + w - t, y, t, len, col);
    ov::rect(p, x, y + h - t, len, t, col);
    ov::rect(p, x, y + h - len, t, len, col);
    ov::rect(p, x + w - len, y + h - t, len, t, col);
    ov::rect(p, x + w - t, y + h - len, t, len, col);
}

// a square key cap, outlined; returns its width
inline float keycap(ov::Paint *p, float x, float y, std::string_view label, float k, uint32_t col = kText)
{
    ov::TextStyle s = ts(12 * k, col, ov::align::center | ov::align::middle, 1.0f * k);
    float w = std::max(28 * k, text_w(p, s, label) + 14 * k), h = 22 * k;
    ov::rect(p, x, y, w, h, 0xFFFFFF0C);
    ov::rect_outline(p, x, y, w, h, 1 * k, alpha(col, 0.55f));
    ov::rect(p, x, y + h - 2 * k, w, 2 * k, alpha(col, 0.55f));
    ov::text(p, x + w / 2, y + h / 2 - 1 * k, s, label);
    return w;
}

} // namespace driver_form::ui
