// SPDX-License-Identifier: MIT
// The messages shown on the race screen: the in and out of form board, and the toasts.
#pragma once
#include <swofty/mixin.hpp>

#include <swofty/hud.hpp>
#include <swofty/registry.hpp>

#include "form_state.hpp"

#include <algorithm>

namespace driver_form {

// The race HUD's notification. The setting only sets how often one may show.
inline void say(const char *kind, const std::string &text, const std::string &detail, const char *icon = "generic")
{
    log("[driver-momentum] msg %s %s", text.c_str(), detail.c_str());
    if (g_message_level == 0 || !option<bool>(kind) || !hud::available()) {
        return;
    }
    auto now = Clock::now();
    constexpr int kSecondsBetweenToasts[] = {0, 60, 30, 15, 4}; // at each setting; every kind can show
    if (now - g_last_toast < std::chrono::seconds(kSecondsBetweenToasts[g_message_level])) {
        return;
    }
    g_last_toast = now;
    hud::toast(text, detail, 3, icon);
}

// Driver Momentum: in a career, the drivers on a hot streak or in a slump, with their last results ("NORRIS P2 P1 P3").
inline void streak_board(const std::vector<const Driver *> &drivers)
{
    if (g_career.empty() || g_level == momentum::Level::off) {
        return;
    }
    struct Named {
        const Driver *driver;
        float streak;
    };
    std::vector<Named> hot;
    std::vector<Named> cold;
    for (const Driver *driver : drivers) {
        auto record = g_book.records.find(driver->stat_id);
        if (record == g_book.records.end() || record->second.races == 0) {
            continue;
        }
        float streak = record->second.streak;
        if (streak >= momentum::kTagStreak) {
            hot.push_back({driver, streak});
        } else if (streak <= -momentum::kTagStreak) {
            cold.push_back({driver, streak});
        }
    }
    auto line = [](std::vector<Named> &list, bool best_first) {
        std::sort(list.begin(), list.end(), [best_first](const Named &a, const Named &b) {
            return best_first ? a.streak > b.streak : a.streak < b.streak;
        });
        std::string text;
        for (size_t i = 0; i < list.size() && i < 2; i++) {
            if (!text.empty()) {
                text += "   ";
            }
            text += list[i].driver->label;
            std::string recent = momentum::recent_text(g_book.records[list[i].driver->stat_id], 3);
            if (!recent.empty()) {
                text += " " + recent;
            }
        }
        return text;
    };
    if (!hot.empty()) {
        std::string text = line(hot, true);
        log("[driver-momentum] board: hot streak %s -> toast %s", text.c_str(),
            hud::name(hud::toast("HOT STREAK", text, 5, "fast_lap")));
    }
    if (!cold.empty()) {
        std::string text = line(cold, false);
        log("[driver-momentum] board: slump %s -> toast %s", text.c_str(),
            hud::name(hud::toast("SLUMP", text, 5, "generic")));
    }
}

// Names the drivers most in form and most out of form, once at the start.
inline void board()
{
    if (g_board_done || g_drivers.empty()) {
        return;
    }
    g_board_done = true;
    if (!option<bool>("form_board") || !hud::available()) {
        return;
    }
    std::vector<const Driver *> order;
    for (const Driver &driver : g_drivers) {
        if (!driver.human) {
            order.push_back(&driver);
        }
    }
    std::sort(order.begin(), order.end(),
              [](const Driver *left, const Driver *right) { return left->form > right->form; });
    std::string in_form;
    std::string out_of_form;
    for (size_t i = 0; i < order.size() && i < 3; i++) {
        if (order[i]->form >= 0.3f) {
            if (!in_form.empty()) {
                in_form += "  ";
            }
            in_form += order[i]->label;
        }
    }
    for (size_t i = 0; i < order.size() && i < 3; i++) {
        const Driver *driver = order[order.size() - 1 - i];
        if (driver->form <= -0.3f) {
            if (!out_of_form.empty()) {
                out_of_form += "  ";
            }
            out_of_form += driver->label;
        }
    }
    if (!in_form.empty()) {
        log("[driver-momentum] board: in form %s -> toast %s", in_form.c_str(),
            hud::name(hud::toast("IN FORM", in_form, 5, "fast_lap")));
    }
    if (!out_of_form.empty()) {
        log("[driver-momentum] board: out of form %s -> toast %s", out_of_form.c_str(),
            hud::name(hud::toast("OUT OF FORM", out_of_form, 5, "generic")));
    }
    streak_board(order);
}

} // namespace driver_form
