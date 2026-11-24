// SPDX-License-Identifier: MIT
// Draws the Form Guide into SVG files without the game: the core's overlay is replaced by a small SVG writer (Silly
// Season's preview writer), and the rows come from careers the engine plays. Text widths are an estimate of the
// game's font (Titillium Web SemiBold). tools\render_svgs.py turns the SVG files into PNG.
//
//   form_guide_preview.exe <out dir>
#include <swofty/mixin.hpp>

#include "../src/form_guide.hpp"
#include "../src/momentum.hpp"

#include <cstdarg>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <random>

namespace ov = swofty::overlay;
using namespace driver_form;

namespace preview {

std::string out;
int next_id = 0;

std::string col(uint32_t rgba, const char *what)
{
    char b[96];
    std::snprintf(b, sizeof b, "%s=\"#%06X\" %s-opacity=\"%.3f\"", what, rgba >> 8, what, (rgba & 0xFF) / 255.0);
    return b;
}

float char_w(uint32_t c)
{
    if (c == ' ')
        return 0.24f;
    if (c == 'i' || c == 'l' || c == 'j' || c == '.' || c == ',' || c == ':' || c == ';' || c == '\'' || c == '!' || c == '|')
        return 0.24f;
    if (c == 'I')
        return 0.27f;
    if (c == 'm' || c == 'w')
        return 0.78f;
    if (c == 'M' || c == 'W')
        return 0.85f;
    if (c >= 'A' && c <= 'Z')
        return 0.62f;
    if (c >= '0' && c <= '9')
        return 0.53f;
    if (c == 'f' || c == 't' || c == 'r')
        return 0.36f;
    return 0.5f;
}

std::vector<uint32_t> codepoints(std::string_view s)
{
    std::vector<uint32_t> cps;
    for (size_t i = 0; i < s.size();) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        uint32_t cp = c;
        int n = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
        if (n > 1) {
            cp = c & (0x3F >> (n - 1));
            for (int k = 1; k < n && i + k < s.size(); ++k)
                cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
        }
        cps.push_back(cp);
        i += n;
    }
    return cps;
}

float line_w(std::string_view s, float px, float tracking)
{
    float w = 0;
    auto cps = codepoints(s);
    for (uint32_t c : cps)
        w += char_w(c >= 0xC0 && c < 0x180 ? 'a' : c) * px + tracking;
    return w;
}

std::vector<std::string> wrap(std::string_view s, float px, float tracking, float max_w)
{
    std::vector<std::string> lines;
    size_t at = 0;
    while (at <= s.size()) {
        size_t nl = s.find('\n', at);
        std::string_view para = s.substr(at, nl == std::string_view::npos ? std::string_view::npos : nl - at);
        if (max_w <= 0) {
            lines.emplace_back(para);
        } else {
            std::string line;
            size_t p = 0;
            while (p <= para.size()) {
                size_t sp = para.find(' ', p);
                std::string_view word = para.substr(p, sp == std::string_view::npos ? std::string_view::npos : sp - p);
                std::string trial = line.empty() ? std::string(word) : line + " " + std::string(word);
                if (!line.empty() && line_w(trial, px, tracking) > max_w) {
                    lines.push_back(line);
                    line = std::string(word);
                } else {
                    line = trial;
                }
                if (sp == std::string_view::npos)
                    break;
                p = sp + 1;
            }
            lines.push_back(line);
        }
        if (nl == std::string_view::npos)
            break;
        at = nl + 1;
    }
    return lines;
}

std::string esc(std::string_view s)
{
    std::string o;
    for (char c : s)
        o += c == '<' ? "&lt;" : c == '>' ? "&gt;" : c == '&' ? "&amp;" : c == '"' ? "&quot;" : std::string(1, c);
    return o;
}

float W = 1920, H = 1080;

int available() { return 1; }
int on_paint(SwoftyPaintFn, void *, uint32_t *) { return SWOFTY_ASSET_OK; }
int remove_(uint32_t) { return SWOFTY_ASSET_OK; }
void size(const SwoftyPaint *, float *w, float *h)
{
    *w = W;
    *h = H;
}
void rect(SwoftyPaint *, float x, float y, float w, float h, uint32_t c)
{
    char b[160];
    std::snprintf(b, sizeof b, "<rect x=\"%.1f\" y=\"%.1f\" width=\"%.1f\" height=\"%.1f\" ", x, y, w, h);
    out += b + col(c, "fill") + "/>\n";
}
void rect_outline(SwoftyPaint *, float x, float y, float w, float h, float t, uint32_t c)
{
    char b[200];
    std::snprintf(b, sizeof b, "<rect x=\"%.1f\" y=\"%.1f\" width=\"%.1f\" height=\"%.1f\" fill=\"none\" stroke-width=\"%.1f\" ", x + t / 2,
                  y + t / 2, w - t, h - t, t);
    out += b + col(c, "stroke") + "/>\n";
}
void line(SwoftyPaint *, float x0, float y0, float x1, float y1, float t, uint32_t c)
{
    char b[200];
    std::snprintf(b, sizeof b, "<line x1=\"%.1f\" y1=\"%.1f\" x2=\"%.1f\" y2=\"%.1f\" stroke-width=\"%.1f\" stroke-linecap=\"round\" ", x0, y0,
                  x1, y1, t);
    out += b + col(c, "stroke") + "/>\n";
}
void measure_styled(const SwoftyPaint *, const SwoftyTextStyle *s, const char *t, float *w, float *h)
{
    auto lines = wrap(t, s->px, s->tracking, s->max_width);
    float mw = 0;
    for (auto &l : lines)
        mw = std::max(mw, line_w(l, s->px, s->tracking));
    *w = mw;
    *h = s->px + (lines.size() - 1) * s->px * s->line_height;
}
void text_styled(SwoftyPaint *, float x, float y, const SwoftyTextStyle *s, const char *t)
{
    auto lines = wrap(t, s->px, s->tracking, s->max_width);
    float h = s->px + (lines.size() - 1) * s->px * s->line_height;
    uint32_t ha = s->align & 0x0f, va = s->align & 0xf0;
    float top = va == ov::align::middle ? y - h / 2 : va == ov::align::bottom ? y - h : va == ov::align::baseline ? y - s->px * 0.78f : y;
    const char *anchor = ha == ov::align::center ? "middle" : ha == ov::align::right ? "end" : "start";
    for (size_t i = 0; i < lines.size(); ++i) {
        char b[260];
        std::snprintf(b, sizeof b,
                      "<text x=\"%.1f\" y=\"%.1f\" font-size=\"%.1f\" letter-spacing=\"%.2f\" text-anchor=\"%s\" ", x,
                      top + i * s->px * s->line_height + s->px * 0.80f, s->px, s->tracking, anchor);
        std::string shadow;
        if (s->shadow_rgba & 0xFF) {
            char f[200];
            std::snprintf(f, sizeof f, " style=\"filter: drop-shadow(%.1fpx %.1fpx %.1fpx rgba(%u,%u,%u,%.2f))\"", s->shadow_dx, s->shadow_dy,
                          s->shadow_blur / 2, s->shadow_rgba >> 24, (s->shadow_rgba >> 16) & 0xFF, (s->shadow_rgba >> 8) & 0xFF,
                          (s->shadow_rgba & 0xFF) / 255.0);
            shadow = f;
        }
        out += b + col(s->rgba, "fill") + shadow + ">" + esc(lines[i]) + "</text>\n";
    }
}
void text(SwoftyPaint *p, float x, float y, float px, uint32_t c, const char *t)
{
    SwoftyTextStyle s{};
    s.px = px;
    s.rgba = c;
    s.line_height = 1.2f;
    text_styled(p, x, y, &s, t);
}
void measure(const SwoftyPaint *p, float px, const char *t, float *w, float *h)
{
    SwoftyTextStyle s{};
    s.px = px;
    s.line_height = 1.2f;
    measure_styled(p, &s, t, w, h);
}
// the addon's two textures, drawn as their SVG look-alikes: the glow as a radial gradient, the shadow as a blur
int load_image(const char *id, const char *)
{
    std::string_view i = id;
    return i == "dm_glow" || i == "dm_shadow" ? SWOFTY_ASSET_OK : SWOFTY_ASSET_INVALID;
}
int image(SwoftyPaint *, const char *id, float x, float y, float w, float h, uint32_t c)
{
    if (std::string_view(id) != "dm_glow")
        return SWOFTY_ASSET_INVALID;
    int n = next_id++;
    char b[600];
    std::snprintf(b, sizeof b,
                  "<defs><radialGradient id=\"r%d\"><stop offset=\"0\" stop-color=\"#%06X\" stop-opacity=\"%.3f\"/>"
                  "<stop offset=\"0.35\" stop-color=\"#%06X\" stop-opacity=\"%.3f\"/>"
                  "<stop offset=\"1\" stop-color=\"#%06X\" stop-opacity=\"0\"/></radialGradient></defs>"
                  "<ellipse cx=\"%.1f\" cy=\"%.1f\" rx=\"%.1f\" ry=\"%.1f\" fill=\"url(#r%d)\"/>\n",
                  n, c >> 8, (c & 0xFF) / 255.0, c >> 8, (c & 0xFF) / 255.0 * 0.4, c >> 8, x + w / 2, y + h / 2, w / 2, h / 2, n);
    out += b;
    return SWOFTY_ASSET_OK;
}
void rect_rounded(SwoftyPaint *, float x, float y, float w, float h, float r, uint32_t c)
{
    char b[200];
    std::snprintf(b, sizeof b, "<rect x=\"%.1f\" y=\"%.1f\" width=\"%.1f\" height=\"%.1f\" rx=\"%.1f\" ", x, y, w, h,
                  std::min(r, std::min(w, h) / 2));
    out += b + col(c, "fill") + "/>\n";
}
void rect_rounded_outline(SwoftyPaint *, float x, float y, float w, float h, float r, float t, uint32_t c)
{
    char b[220];
    std::snprintf(b, sizeof b, "<rect x=\"%.1f\" y=\"%.1f\" width=\"%.1f\" height=\"%.1f\" rx=\"%.1f\" fill=\"none\" stroke-width=\"%.1f\" ",
                  x + t / 2, y + t / 2, w - t, h - t, std::max(0.0f, r - t / 2), t);
    out += b + col(c, "stroke") + "/>\n";
}
void rect_gradient(SwoftyPaint *, float x, float y, float w, float h, float r, uint32_t from, uint32_t to, uint32_t flags)
{
    int id = next_id++;
    bool hz = flags == SWOFTY_GRADIENT_HORIZONTAL;
    char b[400];
    std::snprintf(b, sizeof b,
                  "<defs><linearGradient id=\"g%d\" x1=\"0\" y1=\"0\" x2=\"%d\" y2=\"%d\"><stop offset=\"0\" stop-color=\"#%06X\" stop-opacity=\"%.3f\"/>"
                  "<stop offset=\"1\" stop-color=\"#%06X\" stop-opacity=\"%.3f\"/></linearGradient></defs>\n",
                  id, hz ? 1 : 0, hz ? 0 : 1, from >> 8, (from & 0xFF) / 255.0, to >> 8, (to & 0xFF) / 255.0);
    out += b;
    std::snprintf(b, sizeof b, "<rect x=\"%.1f\" y=\"%.1f\" width=\"%.1f\" height=\"%.1f\" rx=\"%.1f\" fill=\"url(#g%d)\"/>\n", x, y, w, h,
                  std::min(r, std::min(w, h) / 2), id);
    out += b;
}
void circle(SwoftyPaint *, float cx, float cy, float r, uint32_t c)
{
    char b[160];
    std::snprintf(b, sizeof b, "<circle cx=\"%.1f\" cy=\"%.1f\" r=\"%.1f\" ", cx, cy, r);
    out += b + col(c, "fill") + "/>\n";
}
void circle_outline(SwoftyPaint *, float cx, float cy, float r, float t, uint32_t c)
{
    char b[200];
    std::snprintf(b, sizeof b, "<circle cx=\"%.1f\" cy=\"%.1f\" r=\"%.1f\" fill=\"none\" stroke-width=\"%.1f\" ", cx, cy, r - t / 2, t);
    out += b + col(c, "stroke") + "/>\n";
}
void arc(SwoftyPaint *, float cx, float cy, float r, float a0, float a1, float t, uint32_t c)
{
    char b[300];
    if (a1 - a0 >= 6.2831f) {
        std::snprintf(b, sizeof b, "<circle cx=\"%.1f\" cy=\"%.1f\" r=\"%.1f\" fill=\"none\" stroke-width=\"%.1f\" ", cx, cy, r, t);
    } else {
        float x0 = cx + std::cos(a0) * r, y0 = cy + std::sin(a0) * r, x1 = cx + std::cos(a1) * r, y1 = cy + std::sin(a1) * r;
        std::snprintf(b, sizeof b, "<path d=\"M%.1f %.1f A%.1f %.1f 0 %d 1 %.1f %.1f\" fill=\"none\" stroke-width=\"%.1f\" ", x0, y0, r, r,
                      a1 - a0 > 3.14159f ? 1 : 0, x1, y1, t);
    }
    out += b + col(c, "stroke") + "/>\n";
}
void polyline(SwoftyPaint *, const float *xy, uint32_t n, float t, uint32_t c, uint32_t flags)
{
    std::string pts;
    for (uint32_t i = 0; i < n; ++i) {
        char b[48];
        std::snprintf(b, sizeof b, "%.1f,%.1f ", xy[2 * i], xy[2 * i + 1]);
        pts += b;
    }
    char b[64];
    std::snprintf(b, sizeof b, "\" fill=\"none\" stroke-width=\"%.1f\" stroke-linejoin=\"round\" ", t);
    out += std::string(flags ? "<polygon points=\"" : "<polyline points=\"") + pts + b + col(c, "stroke") + "/>\n";
}
void polygon(SwoftyPaint *, const float *xy, uint32_t n, uint32_t c)
{
    std::string pts;
    for (uint32_t i = 0; i < n; ++i) {
        char b[48];
        std::snprintf(b, sizeof b, "%.1f,%.1f ", xy[2 * i], xy[2 * i + 1]);
        pts += b;
    }
    out += "<polygon points=\"" + pts + "\" " + col(c, "fill") + "/>\n";
}
int image_uv(SwoftyPaint *, const char *, float, float, float, float, float, float, float, float, uint32_t) { return SWOFTY_ASSET_UNAVAILABLE; }
int image_nine(SwoftyPaint *, const char *id, float x, float y, float w, float h, float, float, float, float, float scale, uint32_t c)
{
    if (std::string_view(id) != "dm_shadow")
        return SWOFTY_ASSET_INVALID;
    // the texture's shape sits 40 px in from each side and is blurred by about 14 px
    int n = next_id++;
    float m = 40 * scale, blur = 7 * scale;
    char b[460];
    std::snprintf(b, sizeof b,
                  "<defs><filter id=\"f%d\" x=\"-50%%\" y=\"-50%%\" width=\"200%%\" height=\"200%%\"><feGaussianBlur stdDeviation=\"%.1f\"/></filter></defs>"
                  "<rect x=\"%.1f\" y=\"%.1f\" width=\"%.1f\" height=\"%.1f\" rx=\"%.1f\" filter=\"url(#f%d)\" ",
                  n, blur, x + m, y + m, std::max(1.0f, w - 2 * m), std::max(1.0f, h - 2 * m), 14 * scale, n);
    out += b + col(c, "fill") + "/>\n";
    return SWOFTY_ASSET_OK;
}
int image_size(const char *, float *, float *) { return SWOFTY_ASSET_UNAVAILABLE; }
void push_clip(SwoftyPaint *, float x, float y, float w, float h)
{
    int id = next_id++;
    char b[240];
    std::snprintf(b, sizeof b, "<defs><clipPath id=\"c%d\"><rect x=\"%.1f\" y=\"%.1f\" width=\"%.1f\" height=\"%.1f\"/></clipPath></defs><g clip-path=\"url(#c%d)\">\n",
                  id, x, y, w, h, id);
    out += b;
}
void pop_clip(SwoftyPaint *) { out += "</g>\n"; }
void push_transform(SwoftyPaint *, const float *m)
{
    char b[160];
    std::snprintf(b, sizeof b, "<g transform=\"matrix(%f %f %f %f %f %f)\">\n", m[0], m[1], m[2], m[3], m[4], m[5]);
    out += b;
}
void pop_transform(SwoftyPaint *) { out += "</g>\n"; }
void push_opacity(SwoftyPaint *, float o)
{
    char b[64];
    std::snprintf(b, sizeof b, "<g opacity=\"%.3f\">\n", o);
    out += b;
}
void pop_opacity(SwoftyPaint *) { out += "</g>\n"; }
void safe_area(const SwoftyPaint *, float *x, float *y, float *w, float *h)
{
    *x = std::round(W * 0.035f);
    *y = std::round(H * 0.035f);
    *w = W - 2 * *x;
    *h = H - 2 * *y;
}
float ui_scale(const SwoftyPaint *) { return std::min(W / 1920, H / 1080); }
float mouse_x = -1, mouse_y = -1;
int input(const SwoftyPaint *, SwoftyInput *o)
{
    o->x = mouse_x;
    o->y = mouse_y;
    return SWOFTY_ASSET_OK;
}
int focus(int) { return SWOFTY_ASSET_OK; }

const SwoftyOverlay table = {available, on_paint, remove_, size, rect, rect_outline, line, text, measure, load_image, image,
                             rect_rounded, rect_rounded_outline, rect_gradient, circle, circle_outline, arc, polyline, polygon,
                             text_styled, measure_styled, image_uv, image_nine, image_size, push_clip, pop_clip, push_transform,
                             pop_transform, push_opacity, pop_opacity, safe_area, ui_scale, input, focus};

void log_(const char *fmt, ...)
{
    va_list a;
    va_start(a, fmt);
    std::vprintf(fmt, a);
    std::printf("\n");
    va_end(a);
}
int no_setting(const char *, uint32_t, SwoftySettingValue *) { return SWOFTY_ASSET_UNAVAILABLE; }

void begin()
{
    out.clear();
    char b[400];
    std::snprintf(b, sizeof b,
                  "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%.0f\" height=\"%.0f\" viewBox=\"0 0 %.0f %.0f\" "
                  "font-family=\"'Titillium Web', sans-serif\" font-weight=\"600\">\n"
                  "<style>@import url('https://fonts.googleapis.com/css2?family=Titillium+Web:wght@600&amp;display=block');</style>\n",
                  W, H, W, H);
    out += b;
    // a stand-in for the game behind the overlay: a dusky track, the race HUD's tower on the left
    out += "<defs><linearGradient id=\"bg\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#5d7a96\"/>"
           "<stop offset=\"0.45\" stop-color=\"#9aa7a8\"/><stop offset=\"0.46\" stop-color=\"#3c4148\"/>"
           "<stop offset=\"1\" stop-color=\"#23262b\"/></linearGradient></defs>"
           "<rect width=\"100%\" height=\"100%\" fill=\"url(#bg)\"/>\n";
    char tower[200];
    std::snprintf(tower, sizeof tower, "<rect x=\"%.0f\" y=\"%.0f\" width=\"%.0f\" height=\"%.0f\" fill=\"#000000\" fill-opacity=\"0.55\"/>\n",
                  W * 0.03f, H * 0.12f, W * 0.13f, H * 0.62f);
    out += tower;
}

void save(const std::filesystem::path &file)
{
    out += "</svg>\n";
    std::ofstream(file, std::ios::binary) << out;
}

} // namespace preview

namespace {

// Names a race HUD shows, made up for the preview.
const char *kNames[] = {"ACKERMANN", "BELLAMY", "CASTILLO", "DUBOIS", "EKSTROM", "FONTANA", "GALLAGHER", "HARTMANN",
                        "IWASAKI", "JANSSEN", "KOVAC", "LINDQVIST", "MORENO", "NAKAMURA", "OKAFOR", "PETRAKIS",
                        "QUINTERO", "ROSSETTI", "SANTOS", "TREMBLAY", "VALENTE", "WEBER", "ALVAREZ-MONTENEGRO",
                        "BRANDENBURGER", "CHAKRABORTY", "DOMINGUEZ"};

// A career of `rounds` races on a made-up grid, then this weekend's Form Guide rows.
guide::Data career(int rounds, momentum::Level level, unsigned seed, bool career_mode, int cars = 22)
{
    std::mt19937 random(seed);
    std::vector<momentum::Entrant> grid;
    std::map<uint16_t, float> pace;
    for (int i = 0; i < cars; i++) {
        momentum::Entrant e;
        e.stat = uint16_t(i + 1);
        e.team = "team" + std::to_string(i / 2);
        e.ratings = {std::uniform_real_distribution<float>(40, 98)(random), std::uniform_real_distribution<float>(55, 95)(random)};
        e.human = career_mode && i == 13;
        pace[e.stat] = 1.6f - float(i / 2) * 0.16f + std::uniform_real_distribution<float>(0, 0.6f)(random);
        grid.push_back(e);
    }
    momentum::Book book;
    for (int r = 0; r <= rounds; r++) {
        momentum::roll_weekend(book, grid, level, random);
        if (r == rounds || !career_mode)
            break;
        std::vector<std::pair<float, const momentum::Entrant *>> order;
        std::vector<momentum::Result> results;
        for (const auto &e : grid) {
            float form = book.forms.count(e.stat) ? book.forms[e.stat] : 0.0f;
            if (std::uniform_real_distribution<float>(0, 1)(random) < 0.07f) {
                // a retirement now and then; a disqualification or a car not classified now and then too
                int out = std::uniform_int_distribution<int>(0, 5)(random);
                results.push_back({e.stat, e.team, 0, false, out == 0 ? 2 : out == 1 ? 3 : 1});
                continue;
            }
            order.push_back({pace[e.stat] + form * 0.8f + std::normal_distribution<float>(0, 0.45f)(random), &e});
        }
        std::sort(order.begin(), order.end(), [](auto &a, auto &b) { return a.first > b.first; });
        for (size_t i = 0; i < order.size(); i++)
            results.push_back({order[i].second->stat, order[i].second->team, int(i + 1), true});
        momentum::score_race(book, results, false);
    }
    guide::Data data;
    data.career = career_mode;
    data.level = momentum::name_of(level);
    data.level_roll = book.level;
    data.races = book.races;
    data.session = rounds % 3 == 0 ? "RACE" : rounds % 3 == 1 ? "QUALIFYING" : "PRACTICE";
    data.round = career_mode ? rounds + 1 : 0;
    int moments = 0;
    for (const auto &e : grid) {
        guide::Row row;
        row.label = kNames[e.stat - 1];
        row.human = e.human;
        row.form = e.human ? 0.0f : book.forms[e.stat];
        row.pace = row.form < 0 ? -row.form * 1.5f * 0.6f : -row.form * 1.5f; // "Normal" strength
        // a few drivers in a moment, as in a race
        if (!e.human && data.session == "RACE" && (e.stat % 7 == 3) && moments < 3) {
            row.moment = moments == 0 ? guide::Moment::hot : moments == 1 ? guide::Moment::bad : guide::Moment::mistake;
            moments++;
        }
        const momentum::Record *record = nullptr;
        if (career_mode && book.records.count(e.stat) && book.records[e.stat].races > 0)
            record = &book.records[e.stat];
        if (record) {
            row.record = true;
            row.streak = record->streak;
            std::copy(std::begin(record->recent), std::end(record->recent), row.recent);
        }
        row.tag = e.human ? "" : momentum::tag_of(record, row.form, momentum::regularity(e.ratings));
        data.rows.push_back(row);
    }
    std::stable_sort(data.rows.begin(), data.rows.end(), [](const guide::Row &a, const guide::Row &b) {
        if (a.human != b.human)
            return !a.human;
        return a.form > b.form;
    });
    return data;
}

// the compact view's three, as the plugin picks them in a race: the cars around yours and your teammate; outside a
// race (0.2.0, players' reports): the most and the least in form, whose long role ran into the name
guide::Data with_compact(guide::Data d, bool race = true)
{
    size_t you = 0;
    for (size_t i = 0; i < d.rows.size(); i++)
        if (d.rows[i].human)
            you = i;
    const char *race_roles[] = {"AHEAD", "BEHIND", "TEAMMATE"};
    const char *other_roles[] = {"IN FORM", "OUT OF FORM", "TEAMMATE"};
    const char *const *roles = race ? race_roles : other_roles;
    size_t picks[] = {3, 9, 6};
    for (int i = 0; i < 3; i++) {
        guide::Row r = d.rows[std::min(picks[i], d.rows.size() - 1)];
        r.role = roles[i];
        if (i == 0)
            r.moment = guide::Moment::hot;
        d.compact.push_back(r);
    }
    (void)you;
    return d;
}

void shot(const std::filesystem::path &dir, const char *name, float w, float h, const guide::Data &data, float t = 10,
          bool hint = false, float size = 1.0f, bool compact = false)
{
    preview::W = w;
    preview::H = h;
    guide::g_size = size;
    preview::begin();
    // 0.2.0: the strip and the reminder in their HUD editor boxes, at their defaults (the core's answer when it has no
    // placement), scaled by the size the shot asks for
    SwoftyHudSlotState none{};
    none.size = sizeof none;
    if (size != 1.0f) {
        none.found = 1;
        none.enabled = 1;
        none.opacity = 1;
        none.scale = size;
        float ux = 0, uy = 0;
        guide::hs::default_offset(compact ? guide::kStripSlot : guide::kHintSlot, ux, uy);
        none.x = ux;
        none.y = uy;
    }
    if (compact)
        guide::draw_compact(nullptr, data, guide::hs::place(guide::kStripSlot, none, w, h));
    else if (hint)
        guide::draw_hint(nullptr, data, t, guide::hs::place(guide::kHintSlot, none, w, h));
    else
        guide::draw(nullptr, data, t);
    preview::save(dir / name);
    std::printf("  %s (%.0fx%.0f, %zu rows)\n", name, w, h, data.rows.size());
}

} // namespace

int main(int argc, char **argv)
{
    if (argc < 2) {
        std::printf("form_guide_preview <out dir>\n");
        return 2;
    }
    // the overlay's v2/v3 calls ask the core module for its overlay version
    if (!LoadLibraryW(L"swofty_core.dll")) {
        std::printf("swofty_core.dll (the preview's stand-in) is missing next to the exe\n");
        return 1;
    }
    static SwoftyApi api{};
    api.version = SWOFTY_API_VERSION;
    api.log = preview::log_;
    api.setting = preview::no_setting;
    api.overlay = &preview::table;
    swofty::api = &api;
    std::filesystem::path dir = argv[1];
    std::filesystem::create_directories(dir);
    std::printf("form_guide_preview: writing to %s\n", dir.string().c_str());
    shot(dir, "guide_career.svg", 1920, 1080, career(9, momentum::Level::normal, 11, true));
    shot(dir, "guide_career_strong.svg", 1920, 1080, career(16, momentum::Level::strong, 5, true));
    shot(dir, "guide_career_first.svg", 1920, 1080, career(0, momentum::Level::normal, 3, true));
    shot(dir, "guide_grand_prix.svg", 1920, 1080, career(0, momentum::Level::normal, 21, false));
    shot(dir, "guide_career_1440p.svg", 2560, 1440, career(6, momentum::Level::light, 8, true));
    shot(dir, "guide_career_720p.svg", 1280, 720, career(12, momentum::Level::normal, 13, true));
    shot(dir, "guide_career_large.svg", 1920, 1080, career(9, momentum::Level::normal, 11, true), 10, false, 1.16f);
    shot(dir, "guide_opening.svg", 1920, 1080, career(9, momentum::Level::normal, 11, true), 0.3f);
    shot(dir, "hint_career.svg", 1920, 1080, career(9, momentum::Level::normal, 11, true), 3, true);
    shot(dir, "hint_grand_prix.svg", 1920, 1080, career(0, momentum::Level::normal, 21, false), 3, true);
    // 0.1.0 fixes: short screens, an ultrawide one, 26 cars, the compact view, reduced motion, a level changed mid
    // weekend, and the history's codes
    shot(dir, "guide_720p_large.svg", 1280, 720, career(9, momentum::Level::normal, 11, true), 10, false, 1.16f);
    shot(dir, "guide_1080p_small.svg", 1920, 1080, career(9, momentum::Level::normal, 11, true), 10, false, 0.86f);
    shot(dir, "guide_ultrawide.svg", 3440, 1440, career(9, momentum::Level::normal, 11, true));
    shot(dir, "guide_26_cars.svg", 1920, 1080, career(9, momentum::Level::normal, 17, true, 26));
    shot(dir, "guide_26_cars_720p.svg", 1280, 720, career(9, momentum::Level::normal, 17, true, 26), 10, false, 1.16f);
    {
        guide::Data d = career(9, momentum::Level::normal, 11, true);
        d.level = "strong"; // the setting changed this weekend
        shot(dir, "guide_level_changed.svg", 1920, 1080, d);
    }
    shot(dir, "compact_race.svg", 1920, 1080, with_compact(career(9, momentum::Level::normal, 11, true)), 10, false, 1.0f, true);
    shot(dir, "compact_720p.svg", 1280, 720, with_compact(career(9, momentum::Level::normal, 11, true)), 10, false, 1.0f, true);
    shot(dir, "compact_practice.svg", 1920, 1080, with_compact(career(9, momentum::Level::normal, 11, true), false), 10,
         false, 1.0f, true);
    guide::g_reduced = true;
    shot(dir, "guide_reduced_motion_opening.svg", 1920, 1080, career(9, momentum::Level::normal, 11, true), 0.05f);
    guide::g_reduced = false;
    // the visual pass (15 Nov): the compact strip at 720p with long names and a moment of each kind, 26 cars at 720p
    // at the normal size (paged), and a career's first race at 720p
    {
        guide::Data d = with_compact(career(9, momentum::Level::normal, 11, true));
        const char *longer[] = {"ALVAREZ-MONTERO", "BRANDENBURGER", "CHAKRABORTY"};
        guide::Moment moments[] = {guide::Moment::hot, guide::Moment::mistake, guide::Moment::none};
        for (size_t i = 0; i < d.compact.size() && i < 3; i++) {
            d.compact[i].label = longer[i];
            d.compact[i].moment = moments[i];
        }
        shot(dir, "compact_720p_long_names.svg", 1280, 720, d, 10, false, 1.0f, true);
        shot(dir, "compact_1080p_long_names.svg", 1920, 1080, d, 10, false, 1.0f, true);
        shot(dir, "compact_ultrawide.svg", 3440, 1440, d, 10, false, 1.0f, true);
    }
    shot(dir, "guide_26_cars_720p_normal.svg", 1280, 720, career(9, momentum::Level::normal, 17, true, 26));
    shot(dir, "guide_career_first_720p.svg", 1280, 720, career(0, momentum::Level::normal, 3, true));
    return 0;
}
