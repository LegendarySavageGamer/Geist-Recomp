// Geist Recompiled Launcher v2.0.0
//
// Builds Geist.exe on the player's PC from their own legally dumped disc (builder.h) and
// starts it. Ships no game code or data. Layout follows the project's reference: title at
// top left, Play / Settings / Credits / Exit down the left, version and status along the
// bottom, art filling the window.
#include <SDL.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

#include "builder.h"
#include "gci_import.h"
#include "proc.h"
#include "bug_report.h"
#include "../../gcn-recomp/runtime/controls_editor.h"
#include "../../gcn-recomp/runtime/controls_defaults.h"
#include "pc_options.h"
#include "ui.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#include "stb_image.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#endif

namespace fs = std::filesystem;
static const char* kVersion = "2.0.0";
static const char* kVersionLabel = "2.00";  // as the launcher art shows it
static const int W = 1280, H = 720;

// ------------------------------------------------------------------------- palette
static const Color kCyan = {90, 200, 255, 255};
static const Color kCyanSoft = {90, 200, 255, 110};
static const Color kText = {225, 240, 255, 255};
static const Color kTextDim = {150, 175, 205, 255};
static const Color kPanel = {6, 16, 34, 225};
static const Color kGood = {110, 230, 170, 255};
static const Color kBad = {255, 120, 120, 255};

// ------------------------------------------------------------------------- settings
struct Settings {
    std::string iso;
    bool close_on_launch = true;
    std::string path;
    void load(const std::string& p) {
        path = p;
        std::ifstream f(fs::u8path(p));
        std::string line;
        while (std::getline(f, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string k = line.substr(0, eq), v = line.substr(eq + 1);
            if (k == "disc") iso = v;
            else if (k == "close_on_launch") close_on_launch = v == "1";
        }
    }
    void save() const {
        std::ofstream f(fs::u8path(path), std::ios::trunc);
        f << "# Geist Recompiled launcher settings\n";
        f << "disc=" << iso << "\n";
        f << "close_on_launch=" << (close_on_launch ? 1 : 0) << "\n";
    }
};

// ------------------------------------------------------------------------- file picker
static std::string pick_disc_file() {
#ifdef _WIN32
    wchar_t file[4096] = L"";
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = L"GameCube disc image (*.iso;*.gcm;*.ciso)\0*.iso;*.gcm;*.ciso\0All files\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = 4096;
    ofn.lpstrTitle = L"Select your own Geist (USA) disc image";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return {};
    return fs::path(file).u8string();
#else
    const char* e = getenv("GEIST_TEST_ISO");
    return e ? e : "";
#endif
}

static std::string pick_gci_file() {
#ifdef _WIN32
    wchar_t file[4096] = L"";
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = L"GameCube save (*.gci)\0*.gci\0All files\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = 4096;
    ofn.lpstrTitle = L"Select a Geist save (.gci) to import";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return {};
    return fs::path(file).u8string();
#else
    const char* e = getenv("GEIST_TEST_GCI");
    return e ? e : "";
#endif
}

// ------------------------------------------------------------------------- app state
enum class Screen { Main, Report, Controls, Options, Settings, Credits, Building, Message };

struct Button {
    std::string label;
    int icon;  // 0 play, 1 gear, 2 book, 3 power, -1 none
    std::vector<SDL_FPoint> shape;
    float x, y, w, h;
    bool primary = false;
    bool skin = false;  // painted into the skin image: only light it up
};

struct App {
    SDL_Window* win = nullptr;
    SDL_Renderer* r = nullptr;
    Font title, subtitle, button, body, small, heading;
    SDL_Texture* bg = nullptr;
    SDL_Texture* icon = nullptr;  // assets/icon.png: header and window icon
    int bg_w = 0, bg_h = 0;
    BuildPaths paths;
    Settings settings;
    BugReportForm report;
    controls::Editor controls;
    PcOptions options;
    bool controls_ignore_release=false;
    Screen screen = Screen::Main;
    int focus = 0;
    float mx = -1, my = -1;
    bool running = true;
    float t = 0, dt = 0;

    // status bar
    std::string status = "Ready.";
    bool status_ok = true;

    // message box
    std::string msg_title, msg_body;
    Screen msg_return = Screen::Main;

    // build
    std::thread worker;
    std::atomic<bool> building{false}, cancel{false}, build_done{false}, build_ok{false};
    std::mutex bm;
    BuildProgress progress;
    std::string build_error;
    bool launch_after_build = true;

    std::vector<Button> main_buttons;

    // Skin: an image with the menu painted in (assets/skin.ini). Rects in logical space.
    bool skin = false;
    struct Art { SDL_Texture* tex = nullptr; int w = 0, h = 0; };
    std::map<std::string, Art> art;  // settings / credits / building wallpapers
    std::vector<Art> slides;         // assets/slides/*: the slideshow shown while Geist builds
    float slide_t = 0;               // seconds into the slideshow
    float art_fade = 0;              // 0..1, fades in when a panel opens
    std::string art_key;
    SDL_FRect skin_min{}, skin_max{}, skin_close{};
    SDL_FPoint skin_version{28, H - 18.f}, skin_icon{190, H - 24.f}, skin_status{212, H - 18.f};
};

// ------------------------------------------------------------------------- helpers
static std::vector<unsigned char> read_file(const std::string& path) {
    std::ifstream f(fs::u8path(path), std::ios::binary);
    return std::vector<unsigned char>(std::istreambuf_iterator<char>(f), {});
}

static std::vector<SDL_FPoint> hex_button(float x, float y, float w, float h) {
    const float c = h * 0.45f;  // the angled ends
    return {{x, y + h / 2}, {x + c, y}, {x + w - c, y}, {x + w, y + h / 2}, {x + w - c, y + h}, {x + c, y + h}};
}

static void show_message(App& a, const std::string& title, const std::string& body, Screen back = Screen::Main) {
    a.msg_title = title;
    a.msg_body = body;
    a.msg_return = back;
    a.screen = Screen::Message;
    a.focus = 0;
}

static std::vector<std::string> wrap(const Font& f, const std::string& text, float width) {
    std::vector<std::string> lines;
    std::istringstream paras(text);
    std::string para;
    while (std::getline(paras, para)) {
        std::istringstream words(para);
        std::string w, line;
        while (words >> w) {
            std::string t = line.empty() ? w : line + " " + w;
            if (f.width(t) > width && !line.empty()) { lines.push_back(line); line = w; }
            else line = t;
        }
        lines.push_back(line);
    }
    return lines;
}

static std::string shorten_path(const Font& f, const std::string& p, float width) {
    if (f.width(p) <= width) return p;
    std::string s = p;
    while (s.size() > 4 && f.width("..." + s) > width) s.erase(0, 1);
    return "..." + s;
}

// ------------------------------------------------------------------------- icons
static void draw_icon(SDL_Renderer* r, int icon, float cx, float cy, float s, Color c) {
    switch (icon) {
    case 0: ui::poly(r, {{cx - s * 0.35f, cy - s * 0.45f}, {cx + s * 0.45f, cy}, {cx - s * 0.35f, cy + s * 0.45f}}, c); break;
    case 1: {  // gear
        for (int i = 0; i < 8; i++) {
            float a = i * 3.14159265f / 4;
            ui::line(r, cx + std::cos(a) * s * 0.30f, cy + std::sin(a) * s * 0.30f, cx + std::cos(a) * s * 0.50f,
                     cy + std::sin(a) * s * 0.50f, s * 0.16f, c);
        }
        ui::arc(r, cx, cy, s * 0.30f, 0, 6.2832f, s * 0.14f, c);
        break;
    }
    case 2:  // open book
        ui::poly_outline(r, {{cx - s * 0.48f, cy - s * 0.32f}, {cx - s * 0.04f, cy - s * 0.22f}, {cx - s * 0.04f, cy + s * 0.38f}, {cx - s * 0.48f, cy + s * 0.28f}}, s * 0.08f, c);
        ui::poly_outline(r, {{cx + s * 0.04f, cy - s * 0.22f}, {cx + s * 0.48f, cy - s * 0.32f}, {cx + s * 0.48f, cy + s * 0.28f}, {cx + s * 0.04f, cy + s * 0.38f}}, s * 0.08f, c);
        break;
    case 3:  // power
        ui::arc(r, cx, cy + s * 0.05f, s * 0.38f, -3.14159265f * 0.30f, 3.14159265f * 1.30f, s * 0.10f, c);
        ui::line(r, cx, cy - s * 0.48f, cx, cy, s * 0.11f, c);
        break;
    }
}

// ------------------------------------------------------------------------- background
static void draw_background(App& a) {
    SDL_Renderer* r = a.r;
    if (a.bg) {  // the player's or the project's own art: cover the window
        float s = std::max((float)W / a.bg_w, (float)H / a.bg_h);
        SDL_FRect d = {(W - a.bg_w * s) / 2, (H - a.bg_h * s) / 2, a.bg_w * s, a.bg_h * s};
        if (a.skin) {  // the skin's coordinates assume the image fills the window exactly
            SDL_FRect full = {0, 0, (float)W, (float)H};
            SDL_RenderCopyF(r, a.bg, nullptr, &full);
            return;
        }
        SDL_RenderCopyF(r, a.bg, nullptr, &d);
        ui::hgradient(r, 0, 0, 640, H, {2, 6, 16, 200}, {2, 6, 16, 0});
        return;
    }
    // Procedural: night-blue depth with a drifting spirit made of light.
    ui::vgradient(r, 0, 0, W, H, {4, 10, 26, 255}, {2, 5, 14, 255});
    const float t = a.t;
    // distant haze
    ui::glow(r, 900, 300, 520, {20, 60, 140, 70});
    ui::glow(r, 300, 650, 420, {10, 40, 110, 50});
    // ectoplasm ribbons
    for (int k = 0; k < 7; k++) {
        float phase = k * 0.9f;
        for (int i = 0; i < 90; i++) {
            float u = i / 89.0f;
            float x = 640 + u * 700 + std::sin(u * 6 + t * 0.6f + phase) * 60;
            float y = 120 + k * 75 + std::sin(u * 4.5f + t * 0.9f + phase * 1.7f) * 70 + std::cos(t * 0.3f + k) * 20;
            float alpha = std::sin(u * 3.14159f) * (k % 2 ? 26 : 18);
            ui::glow(r, x, y, 26 + 14 * std::sin(u * 9 + t + k), {70, 150, 255, (Uint8)alpha});
        }
    }
    // the ghost: a figure of light, arms open, its body trailing off into ectoplasm
    const float gx = 930 + std::sin(t * 0.5f) * 10, gy = 360 + std::sin(t * 0.8f) * 8;
    auto limb = [&](float x0, float y0, float x1, float y1, float r0, float r1, Color c, int n) {
        for (int i = 0; i <= n; i++) {
            float u = (float)i / n;
            float wob = std::sin(t * 2.3f + u * 6 + x0 * 0.01f) * 3;
            ui::glow(r, gx + x0 + (x1 - x0) * u + wob, gy + y0 + (y1 - y0) * u, r0 + (r1 - r0) * u, c);
        }
    };
    const Color body = {90, 150, 255, 30}, edge = {150, 205, 255, 46};
    limb(0, -205, 0, -150, 34, 30, edge, 6);                       // head and neck
    limb(-8, -150, -78, -128, 26, 22, body, 8);                    // shoulders
    limb(8, -150, 78, -128, 26, 22, body, 8);
    float sway = std::sin(t * 0.7f) * 10;
    limb(-78, -128, -138, -40 + sway, 22, 15, body, 12);           // upper arms
    limb(78, -128, 138, -40 - sway, 22, 15, body, 12);
    limb(-138, -40 + sway, -186, 50 + sway, 15, 10, body, 12);     // forearms
    limb(138, -40 - sway, 186, 50 - sway, 15, 10, body, 12);
    ui::glow(r, gx - 192, gy + 62 + sway, 26, {170, 220, 255, 70}); // hands
    ui::glow(r, gx + 192, gy + 62 - sway, 26, {170, 220, 255, 70});
    limb(0, -140, 0, 40, 64, 52, body, 14);                        // chest and waist
    for (int k = -2; k <= 2; k++)                                  // the trailing body
        limb(k * 18.0f, 40, k * 46.0f + std::sin(t * 0.9f + k) * 40, 300, 46 - std::abs(k) * 8.0f, 8, {80, 140, 255, 22}, 24);
    for (int v = 0; v < 6; v++) {                                  // veins of light
        float a0 = v * 1.047f + t * 0.2f;
        for (int i = 0; i < 14; i++) {
            float u = i / 13.0f;
            ui::glow(r, gx + std::cos(a0 + u * 1.5f) * (14 + 70 * u), gy - 60 + std::sin(a0 + u * 1.2f) * (10 + 60 * u), 5,
                     {255, 170, 90, (Uint8)(120 * (1 - u))});
        }
    }
    ui::glow(r, gx - 12, gy - 212, 6, {200, 240, 255, 230});         // eyes
    ui::glow(r, gx + 12, gy - 212, 6, {200, 240, 255, 230});
    ui::glow(r, gx, gy - 205, 60, {120, 190, 255, 60});
    ui::glow(r, gx, gy - 60, 90, {255, 140, 60, 70});                 // the core
    ui::glow(r, gx, gy - 60, 34, {255, 210, 150, 170});
    ui::glow(r, gx, gy - 60, 12, {255, 250, 230, 255});
    // orbiting sparks
    for (int k = 0; k < 3; k++) {
        float ang = t * (0.9f + k * 0.25f) + k * 2.1f;
        float ox = gx + std::cos(ang) * (250 + k * 30), oy = gy - 60 + std::sin(ang) * (100 + k * 25);
        ui::glow(r, ox, oy, 34, {255, 110, 60, 140});
        ui::glow(r, ox, oy, 10, {255, 230, 200, 230});
        for (int j = 1; j < 18; j++) {  // trail
            float a2 = ang - j * 0.05f;
            ui::glow(r, gx + std::cos(a2) * (250 + k * 30), gy - 60 + std::sin(a2) * (100 + k * 25), 10 - j * 0.4f,
                     {255, 90, 60, (Uint8)(110 - j * 6)});
        }
    }
    // dust motes
    for (int i = 0; i < 70; i++) {
        float x = std::fmod(i * 197.0f + t * (8 + i % 5), (float)W);
        float y = std::fmod(i * 131.0f - t * (5 + i % 7) + 2000, (float)H);
        ui::glow(r, x, y, 3 + i % 3, {150, 200, 255, (Uint8)(40 + (i * 37) % 60)});
    }
    // left side darkening behind the menu
    ui::hgradient(r, 0, 0, 620, H, {2, 6, 16, 190}, {2, 6, 16, 0});
}

// ------------------------------------------------------------------------- chrome
static SDL_HitTestResult hit_test(SDL_Window* w, const SDL_Point* p, void*) {
    int ww, wh;
    SDL_GetWindowSize(w, &ww, &wh);
    float sx = (float)p->x * W / ww, sy = (float)p->y * H / wh;
    if (sy < 40 && sx < W - 130) return SDL_HITTEST_DRAGGABLE;  // title strip, minus the buttons
    return SDL_HITTEST_NORMAL;
}

static App* g_app;
static bool in_rect(const SDL_FRect& r, float x, float y) { return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h; }

static int chrome_hover(float mx, float my) {
    if (g_app && g_app->skin) {
        if (in_rect(g_app->skin_close, mx, my)) return 0;
        if (in_rect(g_app->skin_min, mx, my)) return 1;
        if (in_rect(g_app->skin_max, mx, my)) return 2;
        return -1;
    }
    if (my < 0 || my > 36) return -1;
    if (mx > W - 44 && mx < W - 8) return 0;   // close
    if (mx > W - 88 && mx < W - 52) return 1;  // minimize
    return -1;
}

static void draw_header(App& a) {
    // The window's own header, top left: icon and name (the window is borderless).
    if (a.skin) return;  // the skin's art carries the title
    if (a.icon) {
        SDL_FRect d = {12, 7, 26, 26};
        SDL_RenderCopyF(a.r, a.icon, nullptr, &d);
    }
    a.small.draw(a.r, "Geist Recompiled", a.icon ? 46.f : 14.f, 27, {200, 225, 255, 230});
}

static void draw_chrome(App& a) {
    SDL_Renderer* r = a.r;
    draw_header(a);
    int h = chrome_hover(a.mx, a.my);
    if (a.skin) {  // the image draws the buttons; light up the one under the mouse
        const SDL_FRect* hr = h == 0 ? &a.skin_close : h == 1 ? &a.skin_min : h == 2 ? &a.skin_max : nullptr;
        if (hr) ui::rect(r, hr->x, hr->y, hr->w, hr->h, h == 0 ? Color{200, 50, 60, 120} : Color{120, 190, 255, 45});
        return;
    }
    float cx = W - 26, cy = 20;
    if (h == 0) ui::rect(r, W - 44, 4, 36, 32, {200, 50, 60, 160});
    ui::line(r, cx - 7, cy - 7, cx + 7, cy + 7, 2, kText);
    ui::line(r, cx - 7, cy + 7, cx + 7, cy - 7, 2, kText);
    cx = W - 70;
    if (h == 1) ui::rect(r, W - 88, 4, 36, 32, {255, 255, 255, 30});
    ui::line(r, cx - 8, cy + 1, cx + 8, cy + 1, 2, kText);
    // frame
    ui::poly_outline(r, {{1, 1}, {W - 1.f, 1}, {W - 1.f, H - 1.f}, {1, H - 1.f}}, 2, {60, 130, 220, 120});
}

// ------------------------------------------------------------------------- widgets
static bool draw_button(App& a, const Button& b, bool focused, bool enabled = true) {
    SDL_Renderer* r = a.r;
    bool hover = ui::point_in(b.shape, a.mx, a.my);
    if (b.skin) {
        // The art already draws the button: light it up when it is under the mouse or has
        // the keyboard / controller focus, and leave it exactly as painted otherwise.
        if ((hover || focused) && enabled) {
            ui::poly(r, b.shape, {90, 190, 255, 34});
            for (size_t i = 0; i < b.shape.size(); i++) {
                auto& p = b.shape[i];
                auto& q = b.shape[(i + 1) % b.shape.size()];
                ui::glow_line(r, p.x, p.y, q.x, q.y, 2, {120, 220, 255, 230});
            }
        }
        return hover && enabled;
    }
    bool hot = (hover || focused) && enabled;
    Color fill = b.primary ? Color{14, 70, 140, (Uint8)(hot ? 235 : 200)} : Color{8, 26, 52, (Uint8)(hot ? 230 : 190)};
    if (hot) for (auto& p : b.shape) ui::glow(r, p.x, p.y, 40, {60, 160, 255, 30});
    ui::poly(r, b.shape, fill);
    // a sheen along the top half
    ui::hgradient(r, b.x + b.h * 0.45f, b.y + 3, b.w - b.h * 0.9f, b.h * 0.35f, {120, 200, 255, (Uint8)(hot ? 40 : 18)}, {120, 200, 255, 0});
    Color edge = enabled ? (hot || b.primary ? kCyan : Color{70, 140, 210, 200}) : Color{60, 80, 110, 160};
    for (size_t i = 0; i < b.shape.size(); i++) {
        auto& p = b.shape[i];
        auto& q = b.shape[(i + 1) % b.shape.size()];
        if (hot || b.primary) ui::glow_line(r, p.x, p.y, q.x, q.y, 2, {edge.r, edge.g, edge.b, 200});
        else ui::line(r, p.x, p.y, q.x, q.y, 2, edge);
    }
    Color tc = enabled ? kText : Color{110, 125, 150, 255};
    float tx = b.x + b.h * 0.95f;
    if (b.icon >= 0) {
        draw_icon(r, b.icon, b.x + b.h * 0.95f, b.y + b.h / 2, b.h * 0.42f, tc);
        tx = b.x + b.h * 1.55f;
    } else {
        tx = b.x + (b.w - a.button.width(b.label)) / 2;
    }
    a.button.draw(r, b.label, tx, b.y + b.h / 2 + a.button.ascent() * 0.36f, tc);
    return hover && enabled;
}

static void draw_status_bar(App& a) {
    SDL_Renderer* r = a.r;
    if (!a.skin) {
        ui::rect(r, 0, H - 48, W, 48, {2, 8, 20, 210});
        ui::hgradient(r, 0, H - 48, W, 1, {70, 150, 255, 160}, {70, 150, 255, 20});
        ui::line(r, 162, H - 34, 162, H - 14, 1, {90, 120, 160, 160});
    }
    a.small.draw(r, std::string("Version ") + kVersionLabel, a.skin_version.x, a.skin_version.y, kTextDim);
    float cx = a.skin_icon.x, cy = a.skin_icon.y;
    Color c = a.status_ok ? kCyan : kBad;
    ui::arc(r, cx, cy, 10, 0, 6.2832f, 2, c);
    if (a.status_ok) {
        ui::line(r, cx - 5, cy, cx - 1, cy + 4, 2, c);
        ui::line(r, cx - 1, cy + 4, cx + 6, cy - 4, 2, c);
    } else {
        ui::line(r, cx, cy - 5, cx, cy + 1, 2, c);
        ui::glow(r, cx, cy + 5, 2, c);
    }
    a.small.draw(r, a.status, a.skin_status.x, a.skin_status.y, kText);
}

static void draw_title(App& a) {
    SDL_Renderer* r = a.r;
    if (a.skin) return;  // the skin has its own title
    const std::string t = "GEIST";
    float x = 64, y = 148;
    ui::glow(r, x + 160, y - 40, 170, {40, 120, 255, 70});
    // glow passes
    for (int i = 0; i < 6; i++) {
        float ang = i * 1.0472f;
        a.title.draw(r, t, x + std::cos(ang) * 3, y + std::sin(ang) * 3, {60, 150, 255, 70});
    }
    a.title.draw(r, t, x, y, {235, 245, 255, 255});
    float sw = a.subtitle.width("RECOMPILED");
    float sx = x + (a.title.width(t) - sw) / 2, sy = y + 42;
    a.subtitle.draw(r, "RECOMPILED", sx, sy, kText);
    ui::glow_line(r, x - 10, sy - 9, sx - 16, sy - 9, 2, kCyanSoft);
    ui::glow_line(r, sx + sw + 16, sy - 9, x + a.title.width(t) + 10, sy - 9, 2, kCyanSoft);
}

// The official art for the open panel, filling the right of the window and fading in.
static void draw_panel_art(App& a, const char* key, float dt) {
    if (a.art_key != key) { a.art_key = key; a.art_fade = 0; }
    auto it = a.art.find(key);
    if (it == a.art.end()) return;
    a.art_fade = std::min(1.0f, a.art_fade + dt * 3.0f);
    const App::Art& art = it->second;
    const float rx = 400, ry = 0, rw = W - rx, rh = H - 48;  // above the status bar
    // cover-fit: crop the source to the region's aspect, centred on the art's subject
    float sa = (float)art.w / art.h, ra = rw / rh;
    SDL_Rect src = {0, 0, art.w, art.h};
    if (sa > ra) { src.w = (int)(art.h * ra); src.x = (art.w - src.w) / 2; }
    else { src.h = (int)(art.w / ra); src.y = (art.h - src.h) / 2; }
    // Drawn in vertical strips so its left part fades into the menu side's art: no hard edge.
    const float fade_w = 260;
    const int strips = 52;
    for (int i = 0; i <= strips; i++) {
        float x0 = i < strips ? rx + fade_w * i / strips : rx + fade_w;
        float x1 = i < strips ? rx + fade_w * (i + 1) / strips : rx + rw;
        float u = i < strips ? (i + 0.5f) / strips : 1.0f;
        float alpha = u * u * (3 - 2 * u) * a.art_fade;
        SDL_Rect sr = {src.x + (int)((x0 - rx) / rw * src.w), src.y, std::max(1, (int)((x1 - x0) / rw * src.w + 0.5f)), src.h};
        SDL_FRect dr = {x0, ry, x1 - x0, rh};
        SDL_SetTextureAlphaMod(art.tex, (Uint8)(255 * alpha));
        SDL_RenderCopyF(a.r, art.tex, &sr, &dr);
    }
}

// While Geist builds: the slides, one after another on the right of the window, each held
// for a while with a slow push-in and cross-faded into the next.
static void draw_slideshow(App& a, float dt) {
    if (a.slides.empty()) return;
    // A dark stage behind the slides, feathered into the menu art on the left, so each
    // image stands on its own instead of over the launcher's artwork.
    ui::hgradient(a.r, 400, 0, 160, H - 48, {2, 7, 18, 0}, {2, 7, 18, 238});
    ui::rect(a.r, 560, 0, W - 560, H - 48, {2, 7, 18, 238});
    const float hold = 7.0f, fade = 1.6f, period = hold + fade;
    a.slide_t += dt;
    const int n = (int)a.slides.size();
    const int cur = (int)(a.slide_t / period) % n, next = (cur + 1) % n;
    const float into = std::fmod(a.slide_t, period);
    const float rx = 420, ry = 8, rw = W - rx - 16, rh = H - 48 - 12 - 232 - 16;  // above the build panel
    auto draw = [&](const App::Art& s, float age, float alpha) {
        if (alpha <= 0.0f) return;
        const float zoom = 1.0f + 0.05f * std::min(1.0f, age / period);
        float sc = std::min(rw / s.w, rh / s.h) * zoom;
        float dw = s.w * sc, dh = s.h * sc;
        SDL_FRect d = {rx + (rw - dw) / 2, ry + (rh - dh) / 2, dw, dh};
        SDL_SetTextureAlphaMod(s.tex, (Uint8)(255 * alpha));
        SDL_RenderCopyF(a.r, s.tex, nullptr, &d);
    };
    // smoothstep cross-fade during the last `fade` seconds of each slide
    float u = into > hold ? (into - hold) / fade : 0.0f;
    u = u * u * (3 - 2 * u);
    draw(a.slides[cur], into, 1.0f - u);
    if (u > 0.0f) draw(a.slides[next], into - hold, u);
}

static void draw_panel(App& a, float x, float y, float w, float h, const std::string& heading) {
    SDL_Renderer* r = a.r;
    std::vector<SDL_FPoint> s = {{x + 24, y}, {x + w, y}, {x + w, y + h - 24}, {x + w - 24, y + h}, {x, y + h}, {x, y + 24}};
    Color fill = kPanel;
    if (!a.art_key.empty() && a.art.count(a.art_key)) fill.a = 185;
    ui::poly(r, s, fill);
    for (size_t i = 0; i < s.size(); i++) ui::glow_line(r, s[i].x, s[i].y, s[(i + 1) % s.size()].x, s[(i + 1) % s.size()].y, 1.5f, {80, 170, 255, 170});
    a.heading.draw(r, heading, x + 36, y + 58, kText);
    ui::hgradient(r, x + 36, y + 74, w * 0.5f, 2, kCyan, {90, 200, 255, 0});
}

// ------------------------------------------------------------------------- actions
static void start_build(App& a) {
    if (a.building) return;
    if (a.worker.joinable()) a.worker.join();
    a.cancel = false;
    a.build_done = false;
    a.building = true;
    a.screen = Screen::Building;
    a.focus = 0;
    a.status = "Building Geist from your disc...";
    a.status_ok = true;
    a.worker = std::thread([&a]() {
        std::string err;
        bool ok = build_game(a.paths, a.settings.iso, [&a](const BuildProgress& p) {
            std::lock_guard<std::mutex> l(a.bm);
            a.progress = p;
        }, a.cancel, err);
        {
            std::lock_guard<std::mutex> l(a.bm);
            a.build_error = err;
        }
        a.build_ok = ok;
        a.build_done = true;
        a.building = false;
    });
}

static bool ensure_disc(App& a, bool force_pick) {
    if (!force_pick && !a.settings.iso.empty()) {
        DiscCheck d = check_disc(a.paths, a.settings.iso);
        if (d.ok) return true;
    }
    std::string p = pick_disc_file();
    if (p.empty()) return false;
    DiscCheck d = check_disc(a.paths, p);
    if (!d.ok) {
        show_message(a, "That disc can't be used", d.error);
        a.status = "Disc not accepted.";
        a.status_ok = false;
        return false;
    }
    a.settings.iso = p;
    a.settings.save();
    a.status = "Disc verified: Geist (USA).";
    a.status_ok = true;
    return true;
}

static void launch(App& a) {
    if (!launch_game(a.paths, a.settings.iso)) {
        show_message(a, "Could not start Geist", "Geist.exe could not be started. Try Settings > Rebuild Geist.");
        return;
    }
    a.status = "Geist is running.";
    a.status_ok = true;
    if (a.settings.close_on_launch) a.running = false;
    else SDL_MinimizeWindow(a.win);
}

static void on_play(App& a) {
    if (!ensure_disc(a, false)) return;
    if (game_is_built(a.paths, a.settings.iso)) { launch(a); return; }
    a.launch_after_build = true;
    start_build(a);
}

static std::string controls_path(App& a) {
    std::error_code ec;
    std::string g = (fs::u8path(a.paths.game) / "controls.txt").u8string();
    if (fs::exists(fs::u8path(g), ec)) return g;
    fs::create_directories(fs::u8path(a.paths.game), ec);
    fs::copy_file(fs::u8path(a.paths.sdk) / "controls.txt", fs::u8path(g), ec);
    return g;
}

// ------------------------------------------------------------------------- screens
static const char* kCredits =
    "GEIST RECOMPILED - an unofficial, fan-made PC port.\n"
    "Port, launcher and online multiplayer: ThyHeroOfTime\n"
    " \n"
    "Geist (2005) was developed by n-Space and published by Nintendo. Geist is a trademark of "
    "Nintendo. This project is not affiliated with or endorsed by Nintendo or n-Space. "
    "No game code or data is included: each player builds the port from their own disc.\n"
    " \n"
    "gcn-recomp - GameCube static recompilation toolkit, (c) Ethan Nicholas, MIT License\n"
    "SDL2 - Simple DirectMedia Layer, zlib License\n"
    "LLVM / Clang / llvm-mingw - Apache 2.0 with LLVM Exceptions; mingw-w64 runtime (ZPL / MIT)\n"
    "Python (standalone build) - Python Software Foundation License\n"
    "stb_truetype, stb_image - Sean Barrett, public domain / MIT\n"
    "Fonts: Orbitron and Rajdhani - SIL Open Font License 1.1\n"
    "decomp-toolkit (analysis during development) - Luke Street, MIT / Apache 2.0";

struct SettingRow { std::string label; std::string value; int action; };

static std::vector<SettingRow> settings_rows(App& a) {
    std::string disc = a.settings.iso.empty() ? "Not selected" : a.settings.iso;
    bool built = game_is_built(a.paths, a.settings.iso);
    return {
        {"Graphics", "Display, widescreen, resolution and filtering", 7},
        {"Audio", "Master volume, music, voices and effects", 8},
        {"Game disc", disc, 0},
        {"Geist.exe", built ? "Built and ready" : "Not built yet", 1},
        {"Controls", "Configure keyboard, mouse and controllers", 2},
        {"Game folder", a.paths.game, 3},
        {"Build log", "work/build.log", 4},
        {"Close launcher when Geist starts", a.settings.close_on_launch ? "On" : "Off", 5},
        {"Import save (.gci)", "Copy a Dolphin / GameCube save onto Geist's memory card", 6},
    };
}

static void settings_action(App& a, int action) {
    switch (action) {
    case 0: ensure_disc(a, true); break;
    case 1:
        if (ensure_disc(a, false)) { a.launch_after_build = false; start_build(a); }
        break;
    case 2: a.controls.open(controls_path(a),controls::factory); a.screen=Screen::Controls; break;
    case 3: { std::error_code ec; fs::create_directories(fs::u8path(a.paths.game), ec); proc_open_with_shell(a.paths.game); break; }
    case 4: proc_open_with_shell((fs::u8path(a.paths.work) / "build.log").u8string()); break;
    case 5: a.settings.close_on_launch = !a.settings.close_on_launch; a.settings.save(); break;
    case 6: {
        const std::string gci = pick_gci_file();
        if (gci.empty()) break;
        const std::string card = (fs::u8path(a.paths.game) / "saves" / "memcard_a.raw").u8string();
        const std::string err = gci_import(gci, card);
        if (err.empty()) show_message(a, "Save imported", "The save is on Geist's memory card now. Load it from the game's menu.\n(The card from before is kept as saves/memcard_a.raw.before-import.raw.)", Screen::Settings);
        else show_message(a, "Could not import the save", err, Screen::Settings);
        break;
    }
    case 7:case 8:{std::error_code ec;fs::create_directories(fs::u8path(a.paths.game),ec);a.options.open((fs::u8path(a.paths.game)/"settings.ini").u8string(),action==8);a.screen=Screen::Options;break;}
    }
}

static std::map<std::string, std::string> g_skin;
static bool skin_rect(const char* key, float sx, float sy, float& x0, float& y0, float& x1, float& y1) {
    auto it = g_skin.find(key);
    if (it == g_skin.end()) return false;
    float v[4];
    if (sscanf(it->second.c_str(), "%f,%f,%f,%f", &v[0], &v[1], &v[2], &v[3]) != 4) return false;
    x0 = v[0] * sx; y0 = v[1] * sy; x1 = v[2] * sx; y1 = v[3] * sy;
    return true;
}
static bool skin_point(const char* key, float sx, float sy, SDL_FPoint& p) {
    auto it = g_skin.find(key);
    float x, y;
    if (it == g_skin.end() || sscanf(it->second.c_str(), "%f,%f", &x, &y) != 2) return false;
    p = {x * sx, y * sy};
    return true;
}

static void layout_main(App& a) {
    a.main_buttons.clear();
    const char* labels[] = {"Play", "Report a Bug", "Settings", "Credits", "Exit"};
    if (a.skin) {
        // The skin image paints the five buttons; skin.ini says where, in its own pixels.
        const float sx = (float)W / a.bg_w, sy = (float)H / a.bg_h;
        const char* keys[] = {"play", "report", "settings", "credits", "exit"};
        bool ok = true;
        for (int i = 0; i < 5 && ok; i++) {
            float x0, y0, x1, y1;
            ok = skin_rect(keys[i], sx, sy, x0, y0, x1, y1);
            if (!ok) break;
            Button b;
            b.label = labels[i]; b.icon = -1; b.primary = i == 0; b.skin = true;
            b.x = x0; b.y = y0; b.w = x1 - x0; b.h = y1 - y0;
            b.shape = hex_button(b.x, b.y, b.w, b.h);
            a.main_buttons.push_back(b);
        }
        float x0, y0, x1, y1;
        if (ok && skin_rect("close", sx, sy, x0, y0, x1, y1)) a.skin_close = {x0, y0, x1 - x0, y1 - y0};
        if (ok && skin_rect("minimize", sx, sy, x0, y0, x1, y1)) a.skin_min = {x0, y0, x1 - x0, y1 - y0};
        if (ok && skin_rect("maximize", sx, sy, x0, y0, x1, y1)) a.skin_max = {x0, y0, x1 - x0, y1 - y0};
        skin_point("version", sx, sy, a.skin_version);
        skin_point("status_icon", sx, sy, a.skin_icon);
        skin_point("status", sx, sy, a.skin_status);
        if (ok) return;
        a.skin = false;  // incomplete skin.ini: fall back to the built-in look
        a.main_buttons.clear();
    }
    for (int i = 0; i < 5; i++) {
        Button b;
        b.label = labels[i];
        b.icon = i==0?0:i==1?-1:i-1;
        b.primary = i == 0;
        b.w = 325;
        b.h = 52;
        b.x = 51;
        b.y = 260 + i * 62;
        b.shape = hex_button(b.x, b.y, b.w, b.h);
        a.main_buttons.push_back(b);
    }
}

static void activate_main(App& a, int i) {
    switch (i) {
    case 0: on_play(a); break;
    case 1: a.report.open(a.paths.root, kVersion); a.screen = Screen::Report; break;
    case 2: a.screen = Screen::Settings; a.focus = 0; break;
    case 3: a.screen = Screen::Credits; a.focus = 0; break;
    case 4: a.running = false; break;
    }
}

static Button make_btn(const std::string& label, float x, float y, float w, float h, bool primary = false) {
    Button b;
    b.label = label; b.icon = -1; b.x = x; b.y = y; b.w = w; b.h = h; b.primary = primary;
    b.shape = hex_button(x, y, w, h);
    return b;
}

// One frame. `click` is set when the mouse button was released this frame; `nav` is
// -1/+1 for up/down, `enter`/`back` for confirm/cancel from keyboard or controller.
static void frame(App& a, bool click, int nav, bool enter, bool back) {
    SDL_Renderer* r = a.r;
    draw_background(a);
    draw_title(a);
    if (!a.skin) ui::rect(r, 35, 250, 365, 335, {3, 13, 30, 255});

    if (a.build_done.exchange(false)) {
        if (a.build_ok) {
            a.status = "Geist.exe is ready.";
            a.status_ok = true;
            if (a.launch_after_build) { a.screen = Screen::Main; launch(a); }
            else show_message(a, "Build complete", "Geist.exe was rebuilt from your disc and is ready to play.", Screen::Settings);
        } else {
            std::string err;
            { std::lock_guard<std::mutex> l(a.bm); err = a.build_error; }
            a.status = err == "Cancelled." ? "Build cancelled." : "Build failed.";
            a.status_ok = err == "Cancelled.";
            if (err != "Cancelled.") show_message(a, "The build did not finish", err + "\n\nDetails are in work/build.log (Settings > Build log).");
            else a.screen = Screen::Main;
        }
    }

    switch (a.screen) {
    case Screen::Main: {
        a.art_key.clear();
        int n = (int)a.main_buttons.size();
        if (nav) a.focus = (a.focus + nav + n) % n;
        for (int i = 0; i < n; i++) {
            bool hover = draw_button(a, a.main_buttons[i], a.focus == i);
            if (hover && (a.mx != -1)) a.focus = i;
            if (hover && click) activate_main(a, i);
        }
        if (enter) activate_main(a, a.focus);
        if (back) a.running = false;
        break;
    }
    case Screen::Report: {
        for (int i=0;i<(int)a.main_buttons.size();++i) draw_button(a,a.main_buttons[i],i==1,false);
        if (a.report.draw(r,a.heading,a.body,a.small,a.mx,a.my,click,nav,enter,back)) {
            a.screen=Screen::Main; a.focus=1;
        }
        break;
    }
    case Screen::Controls: {
        draw_panel(a,472,96,770,540,"CONTROLS");
        for(int i=0;i<(int)a.main_buttons.size();++i)draw_button(a,a.main_buttons[i],i==2,false);
        auto& ed=a.controls;auto cells=controls::layout(ed);
        if(nav)ed.focus=(ed.focus+nav+(int)cells.size())%(int)cells.size();
        for(int i=0;i<(int)cells.size();++i){auto& c=cells[i];float x=512+c.x,y=172+c.y;
            bool hover=a.mx>=x&&a.mx<x+c.w&&a.my>=y&&a.my<y+c.h;
            if(hover)ed.focus=i;bool hot=ed.focus==i||c.binding==ed.selected;
            ui::rect(r,x,y,c.w,c.h,hot?Color{35,95,170,210}:Color{10,30,60,220});
            if(c.slider){ui::rect(r,x+8,y+c.h-5,c.w-16,3,kTextDim);ui::rect(r,x+8+(c.w-16)*controls::slider_fraction(ed)-3,y+c.h-9,6,11,kCyan);}
            a.small.draw(r,shorten_path(a.small,c.label,c.w-16),x+8,y+21,hot?kCyan:kText);
            if(!ed.capturing&&!ed.conflict){if(c.slider&&hover&&(click||(SDL_GetMouseState(nullptr,nullptr)&SDL_BUTTON_LMASK)))controls::set_slider(ed,(a.mx-x-8)/(c.w-16));else if((hover&&click)||(ed.focus==i&&enter))controls::activate(ed,c);}
        }
        std::string s=(ed.dirty()?"Unsaved changes. ":"")+ed.status;
        if(ed.capturing)s="PRESS A KEY OR BUTTON TO ASSIGN. Esc cancels.";
        a.small.draw(r,shorten_path(a.small,s,690),512,620,kCyan);
        if(back&&!ed.capturing&&!ed.conflict)ed.command(controls::Editor::Cancel);
        if(ed.closed){a.screen=Screen::Settings;a.focus=2;}
        break;
    }
    case Screen::Options: {
        draw_panel(a,472,96,770,540,a.options.audio?"AUDIO":"GRAPHICS & PERFORMANCE");
        for(int i=0;i<(int)a.main_buttons.size();++i)draw_button(a,a.main_buttons[i],i==2,false);
        auto& o=a.options;int count=(int)o.options.size();if(nav)o.focus=(o.focus+nav+count+4)%(count+4);
        float spacing=std::min(40.f,340.f/std::max(1,count));
        for(int i=0;i<count;++i){float y=175+i*spacing;bool hover=a.mx>=505&&a.mx<=1205&&a.my>=y&&a.my<y+spacing-2;if(hover)o.focus=i;bool hot=o.focus==i;
            if(hot)ui::rect(r,505,y,700,spacing-2,{40,110,210,60});
            a.body.draw(r,o.options[i].label,515,y+spacing*.72f,kText);
            a.small.draw(r,shorten_path(a.small,o.value(i),340),845,y+spacing*.72f,hot?kCyan:kTextDim);
            if((hover&&click)||(hot&&enter))o.change(i,1);
        }
        a.small.draw(r,shorten_path(a.small,o.status,690),510,545,kTextDim);
        auto defaults=make_btn("Defaults",510,566,150,44),apply=make_btn("Apply",675,566,110,44),save=make_btn("Save & back",805,566,180,44),cancel=make_btn("Cancel",1015,566,180,44);
        if((draw_button(a,defaults,o.focus==count)&&click)||(o.focus==count&&enter))o.restore();
        if((draw_button(a,apply,o.focus==count+1)&&click)||(o.focus==count+1&&enter))o.save();
        if((draw_button(a,save,o.focus==count+2)&&click)||(o.focus==count+2&&enter)){if(o.save()){a.screen=Screen::Settings;a.focus=0;}}
        if((draw_button(a,cancel,o.focus==count+3)&&click)||(o.focus==count+3&&enter)||back){a.screen=Screen::Settings;a.focus=0;}
        break;
    }
    case Screen::Settings: {
        float px = 472, py = 96, pw = 770, ph = 540;
        draw_panel_art(a, "settings_art", a.dt);
        draw_panel(a, px, py, pw, ph, "SETTINGS");
        auto rows = settings_rows(a);
        int n = (int)rows.size() + 1;  // + Back
        if (nav) a.focus = (a.focus + nav + n) % n;
        for (int i = 0; i < (int)rows.size(); i++) {
            float ry = py + 76 + i * 40;
            bool hover = a.mx > px + 20 && a.mx < px + pw - 20 && a.my > ry && a.my < ry + 38;
            if (hover) a.focus = i;
            bool hot = a.focus == i;
            if (hot) ui::rect(r, px + 24, ry, pw - 48, 38, {40, 110, 210, 60});
            if (hot) ui::rect(r, px + 24, ry, 3, 38, kCyan);
            a.body.draw(r, rows[i].label, px + 40, ry + 16, kText);
            Color vc = rows[i].action == 1 ? (rows[i].value[0] == 'B' ? kGood : kTextDim) : kTextDim;
            a.small.draw(r, shorten_path(a.small, rows[i].value, pw - 300), px + 40, ry + 34, vc);
            const char* act[] = {"Change...", "Rebuild", "Open", "Open", "Open", "Toggle", "Import...", "Configure", "Configure"};
            float aw = a.small.width(act[rows[i].action]);
            a.small.draw(r, act[rows[i].action], px + pw - 48 - aw, ry + 30, hot ? kCyan : kTextDim);
            if ((hover && click) || (hot && enter)) settings_action(a, rows[i].action);
        }
        Button bb = make_btn("Back", px + pw - 220, py + ph - 76, 180, 50);
        bool bh = draw_button(a, bb, a.focus == n - 1);
        if (bh) a.focus = n - 1;
        if ((bh && click) || (a.focus == n - 1 && enter) || back) { a.screen = Screen::Main; a.focus = 2; }
        // still allow the main menu to be seen on the left
        for (int i = 0; i < (int)a.main_buttons.size(); i++) draw_button(a, a.main_buttons[i], i == 2, false);
        break;
    }
    case Screen::Credits: {
        float px = 472, py = 96, pw = 770, ph = 540;
        draw_panel_art(a, "credits_art", a.dt);
        draw_panel(a, px, py, pw, ph, "CREDITS");
        float y = py + 110;
        for (auto& line : wrap(a.small, kCredits, pw - 80)) {
            a.small.draw(r, line, px + 40, y, kText);
            y += 25;
        }
        Button bb = make_btn("Back", px + pw - 220, py + ph - 76, 180, 50);
        bool bh = draw_button(a, bb, true);
        if ((bh && click) || enter || back) { a.screen = Screen::Main; a.focus = 3; }
        for (int i = 0; i < (int)a.main_buttons.size(); i++) draw_button(a, a.main_buttons[i], i == 3, false);
        break;
    }
    case Screen::Building: {
        BuildProgress p;
        { std::lock_guard<std::mutex> l(a.bm); p = a.progress; }
        // With slides: a compact panel along the bottom and the slideshow above it.
        // Without: the original panel in the middle over the building art.
        const bool slides = !a.slides.empty();
        float px = 472, pw = 770, ph = slides ? 232 : 330;
        float py = slides ? H - 48 - 12 - ph : 190;
        if (slides) { a.art_key.clear(); draw_slideshow(a, a.dt); }
        else draw_panel_art(a, "building_art", a.dt);
        draw_panel(a, px, py, pw, ph, "BUILDING GEIST");
        const float y_desc = slides ? 94 : 116, y_stage = slides ? 126 : 152, y_bar = slides ? 150 : 190;
        a.body.draw(r, "Recompiling your disc into a native PC game. This happens once.", px + 40, py + y_desc, kTextDim);
        const char* stages[] = {"", "Verify disc", "Recompile", "Compile", "Link", "Finish"};
        for (int s = 1; s <= 5; s++) {
            float sx = px + 40 + (s - 1) * 146;
            Color c = s < p.stage ? kGood : s == p.stage ? kCyan : Color{90, 110, 140, 255};
            ui::arc(r, sx + 8, py + y_stage, 7, 0, 6.2832f, 2, c);
            if (s < p.stage) ui::glow(r, sx + 8, py + y_stage, 4, c);
            a.small.draw(r, stages[s], sx + 24, py + y_stage + 7, c);
        }
        float bx = px + 40, by = py + y_bar, bw = pw - 80, bh = slides ? 22 : 26;
        ui::rect(r, bx, by, bw, bh, {10, 30, 60, 255});
        float fw = bw * p.fraction;
        ui::hgradient(r, bx, by, fw, bh, {30, 110, 230, 255}, {110, 220, 255, 255});
        ui::glow(r, bx + fw, by + bh / 2, 30, {120, 210, 255, 120});
        ui::poly_outline(r, {{bx, by}, {bx + bw, by}, {bx + bw, by + bh}, {bx, by + bh}}, 1.5f, {80, 160, 240, 200});
        char pct[16];
        snprintf(pct, sizeof pct, "%d%%", (int)(p.fraction * 100));
        a.body.draw(r, pct, bx + bw - a.body.width(pct), by + bh + (slides ? 26 : 30), kText);
        a.small.draw(r, shorten_path(a.small, p.message, bw - 90), bx, by + bh + (slides ? 24 : 28), kTextDim);
        Button cb = slides ? make_btn(a.cancel ? "Cancelling..." : "Cancel", px + pw - 200, py + 22, 170, 42)
                           : make_btn(a.cancel ? "Cancelling..." : "Cancel", px + pw - 220, py + ph - 70, 180, 48);
        bool ch = draw_button(a, cb, true, !a.cancel);
        if ((ch && click) || back) a.cancel = true;
        for (int i = 0; i < (int)a.main_buttons.size(); i++) draw_button(a, a.main_buttons[i], false, false);
        a.status = p.message.empty() ? "Building..." : p.message;
        break;
    }
    case Screen::Message: {
        a.art_key.clear();
        float px = 472, py = 180, pw = 770, ph = 350;
        draw_panel(a, px, py, pw, ph, a.msg_title);
        float y = py + 118;
        for (auto& line : wrap(a.body, a.msg_body, pw - 80)) {
            a.body.draw(r, line, px + 40, y, kText);
            y += 30;
        }
        Button ok = make_btn("OK", px + pw - 220, py + ph - 76, 180, 50, true);
        bool oh = draw_button(a, ok, true);
        if ((oh && click) || enter || back) { a.screen = a.msg_return; a.focus = 0; }
        for (int i = 0; i < (int)a.main_buttons.size(); i++) draw_button(a, a.main_buttons[i], false, false);
        break;
    }
    }

    draw_status_bar(a);
    draw_chrome(a);
}

// ------------------------------------------------------------------------- main
static bool load_image(SDL_Renderer* r, const std::vector<unsigned char>& bytes, SDL_Texture*& tex, int& w, int& h) {
    int n;
    if (bytes.empty()) return false;
    unsigned char* px = stbi_load_from_memory(bytes.data(), (int)bytes.size(), &w, &h, &n, 4);
    if (!px) return false;
    tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, w, h);
    SDL_UpdateTexture(tex, nullptr, px, w * 4);
    SDL_SetTextureScaleMode(tex, SDL_ScaleModeLinear);
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    stbi_image_free(px);
    return true;
}

static bool load_assets(App& a) {
    const std::string dir = (fs::u8path(a.paths.root) / "assets").u8string();
    auto f = [&](const char* n) { return read_file((fs::u8path(dir) / n).u8string()); };
    auto orb = f("Orbitron.ttf"), semi = f("Rajdhani-SemiBold.ttf"), med = f("Rajdhani-Medium.ttf");
    if (orb.empty() || semi.empty() || med.empty()) return false;
    a.title.load(a.r, orb, 92, 10);
    a.subtitle.load(a.r, semi, 30, 9);
    a.heading.load(a.r, orb, 30, 4);
    a.button.load(a.r, semi, 34, 4);
    a.body.load(a.r, med, 24, 0.5f);
    a.small.load(a.r, med, 20, 0.5f);
    {
        std::ifstream sf(fs::u8path(dir) / "skin.ini");
        std::string line;
        while (std::getline(sf, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty() || line[0] == '#') continue;
            auto eq = line.find('=');
            if (eq != std::string::npos) g_skin[line.substr(0, eq)] = line.substr(eq + 1);
        }
    }
    std::vector<std::string> names = {"background.png", "background.jpg"};
    if (g_skin.count("image")) names.insert(names.begin(), g_skin["image"]);
    for (const std::string& nm : names) {
        const char* name = nm.c_str();
        int w, h, n;
        std::vector<unsigned char> bytes = f(name);
        if (bytes.empty()) continue;
        unsigned char* px = stbi_load_from_memory(bytes.data(), (int)bytes.size(), &w, &h, &n, 4);
        if (!px) continue;
        a.bg = SDL_CreateTexture(a.r, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, w, h);
        SDL_UpdateTexture(a.bg, nullptr, px, w * 4);
        SDL_SetTextureScaleMode(a.bg, SDL_ScaleModeLinear);
        a.bg_w = w;
        a.bg_h = h;
        stbi_image_free(px);
        a.skin = g_skin.count("play") > 0;
        break;
    }
    {
        int w, h;
        load_image(a.r, f("icon.png"), a.icon, w, h);
    }
    for (const char* key : {"settings_art", "credits_art", "building_art"}) {
        auto it = g_skin.find(key);
        if (it == g_skin.end()) continue;
        App::Art art;
        if (load_image(a.r, f(it->second.c_str()), art.tex, art.w, art.h)) a.art[key] = art;
    }
    // Build slideshow: every PNG / JPEG in assets/slides, in name order.
    {
        std::vector<fs::path> files;
        std::error_code ec;
        for (auto& e : fs::directory_iterator(fs::u8path(dir) / "slides", ec)) {
            auto ext = e.path().extension().u8string();
            for (auto& ch : ext) ch = (char)tolower((unsigned char)ch);
            if (ext == ".png" || ext == ".jpg" || ext == ".jpeg") files.push_back(e.path());
        }
        std::sort(files.begin(), files.end());
        for (auto& fp : files) {
            App::Art art;
            if (load_image(a.r, read_file(fp.u8string()), art.tex, art.w, art.h)) a.slides.push_back(art);
        }
    }
    return true;
}

int main(int argc, char** argv) {
    App a;
    g_app = &a;
    a.paths = BuildPaths::from_root(proc_exe_dir());
    if (const char* e = getenv("GEIST_ROOT")) a.paths = BuildPaths::from_root(e);
    a.settings.load((fs::u8path(a.paths.root) / "launcher_settings.ini").u8string());

    // Headless build: GeistLauncher --build "<disc image>"
    for (int i = 1; i + 1 < argc; i++)
        if (std::string(argv[i]) == "--build") {
            std::atomic<bool> cancel{false};
            std::string err;
            bool ok = build_game(a.paths, argv[i + 1], [](const BuildProgress& p) {
                printf("%3d%% %s\n", (int)(p.fraction * 100), p.message.c_str());
                fflush(stdout);
            }, cancel, err);
            if (!ok) fprintf(stderr, "%s\n", err.c_str());
            return ok ? 0 : 1;
        }

    const char* shot = getenv("GEIST_SCREENSHOT");  // render one screen to a BMP (testing)
    SDL_SetHint(SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS, "0");
    if (SDL_Init(shot ? 0 : (SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER)) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Surface* surface = nullptr;
    if (shot) {
        surface = SDL_CreateRGBSurfaceWithFormat(0, W, H, 32, SDL_PIXELFORMAT_ARGB8888);
        a.r = SDL_CreateSoftwareRenderer(surface);
    } else {
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
        a.win = SDL_CreateWindow("Geist Recompiled", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W, H,
                                 SDL_WINDOW_BORDERLESS | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE);
        SDL_SetWindowMinimumSize(a.win, 960, 540);
        {
            std::vector<unsigned char> ib = read_file((fs::u8path(a.paths.root) / "assets" / "icon.png").u8string());
            int w, h, n;
            if (unsigned char* px = ib.empty() ? nullptr : stbi_load_from_memory(ib.data(), (int)ib.size(), &w, &h, &n, 4)) {
                SDL_Surface* s = SDL_CreateRGBSurfaceWithFormatFrom(px, w, h, 32, w * 4, SDL_PIXELFORMAT_ABGR8888);
                if (s) { SDL_SetWindowIcon(a.win, s); SDL_FreeSurface(s); }
                stbi_image_free(px);
            }
        }
        SDL_SetWindowHitTest(a.win, hit_test, nullptr);
        a.r = SDL_CreateRenderer(a.win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if (!a.r) a.r = SDL_CreateRenderer(a.win, -1, SDL_RENDERER_SOFTWARE);
    }
    SDL_RenderSetLogicalSize(a.r, W, H);
    SDL_SetRenderDrawBlendMode(a.r, SDL_BLENDMODE_BLEND);
    ui::init(a.r);
    if (!load_assets(a)) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Geist Recompiled", "The assets folder is missing. Reinstall Geist Recompiled.", a.win);
        return 1;
    }
    layout_main(a);

    // Opening status: what Play will do.
    if (manifest_get(a.paths, "disc_id").empty()) { a.status = "The sdk folder is missing. Reinstall Geist Recompiled."; a.status_ok = false; }
    else if (a.settings.iso.empty()) a.status = "Press Play to select your Geist (USA) disc image.";
    else if (game_is_built(a.paths, a.settings.iso)) a.status = "Ready to play Geist Recompiled.";
    else a.status = "Press Play to build Geist from your disc (one time).";

    if (shot) {  // testing: GEIST_SCREEN=main|settings|credits|building|message
        std::string which = getenv("GEIST_SCREEN") ? getenv("GEIST_SCREEN") : "main";
        a.t = 4.0f;
        if (which == "report") { a.screen=Screen::Report; a.report.open(a.paths.root,kVersion); }
        if (which == "controls") {a.screen=Screen::Controls;a.controls.open(controls_path(a),controls::factory);}
        if(which=="graphics"||which=="audio"){a.options.open((fs::u8path(a.paths.game)/"settings.ini").u8string(),which=="audio");a.screen=Screen::Options;}
        if (which == "settings") a.screen = Screen::Settings;
        if (which == "credits") a.screen = Screen::Credits;
        if (which == "building") { a.screen = Screen::Building; a.progress = {3, 5, 0.47f, "Compiling 27 of 60"}; }
        if (which == "message") show_message(a, "That disc can't be used", "This is not Geist (GITE01).\nThe file's disc ID is \"GZLE01\".");
        a.mx = 200; a.my = 290;
        a.dt = 1.0f;
        SDL_SetRenderDrawColor(a.r, 0, 0, 0, 255);
        SDL_RenderClear(a.r);
        frame(a, false, 0, false, false);
        SDL_RenderPresent(a.r);
        SDL_SaveBMP(surface, shot);
        return 0;
    }

    SDL_GameController* pad = nullptr;
    Uint32 last = SDL_GetTicks();
    while (a.running) {
        bool click = false, enter = false, back = false;
        int nav = 0;
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if(a.screen==Screen::Controls && e.type!=SDL_QUIT && a.controls.event(e)) {
                if(e.type==SDL_MOUSEBUTTONDOWN)a.controls_ignore_release=true;
                continue;
            }
            if(a.controls_ignore_release&&e.type==SDL_MOUSEBUTTONUP){a.controls_ignore_release=false;continue;}
            switch (e.type) {
            case SDL_QUIT: a.running = false; break;
            case SDL_MOUSEMOTION: {
                float lx, ly;
                SDL_RenderWindowToLogical(a.r, e.motion.x, e.motion.y, &lx, &ly);
                a.mx = lx; a.my = ly;
                break;
            }
            case SDL_MOUSEBUTTONUP:
                if (e.button.button == SDL_BUTTON_LEFT) {
                    int ch = chrome_hover(a.mx, a.my);
                    if (ch == 0) a.running = false;
                    else if (ch == 1) SDL_MinimizeWindow(a.win);
                    else if (ch == 2) {
                        if (SDL_GetWindowFlags(a.win) & SDL_WINDOW_MAXIMIZED) SDL_RestoreWindow(a.win);
                        else SDL_MaximizeWindow(a.win);
                    } else click = true;
                }
                break;
            case SDL_TEXTINPUT:
                if (a.screen == Screen::Report) a.report.event(e);
                break;
            case SDL_KEYDOWN:
                if (a.screen == Screen::Report && a.report.event(e)) break;
                if(e.key.keysym.scancode==SDL_SCANCODE_LEFT||e.key.keysym.scancode==SDL_SCANCODE_RIGHT){
                    int d=e.key.keysym.scancode==SDL_SCANCODE_LEFT?-1:1;
                    if(a.screen==Screen::Controls){a.controls.edit_option("gain",d*.01f);break;}
                    if(a.screen==Screen::Options&&a.options.focus<(int)a.options.options.size()){a.options.change(a.options.focus,d);break;}
                }
                switch (e.key.keysym.scancode) {
                case SDL_SCANCODE_UP: case SDL_SCANCODE_W: nav = -1; break;
                case SDL_SCANCODE_DOWN: case SDL_SCANCODE_S: case SDL_SCANCODE_TAB: nav = 1; break;
                case SDL_SCANCODE_RETURN: case SDL_SCANCODE_SPACE: case SDL_SCANCODE_KP_ENTER: enter = true; break;
                case SDL_SCANCODE_ESCAPE: case SDL_SCANCODE_BACKSPACE: back = true; break;
                default: break;
                }
                a.mx = a.my = -1;
                break;
            case SDL_CONTROLLERDEVICEADDED:
                if (!pad) pad = SDL_GameControllerOpen(e.cdevice.which);
                break;
            case SDL_CONTROLLERBUTTONDOWN:
                if(e.cbutton.button==SDL_CONTROLLER_BUTTON_DPAD_LEFT||e.cbutton.button==SDL_CONTROLLER_BUTTON_DPAD_RIGHT){
                    int d=e.cbutton.button==SDL_CONTROLLER_BUTTON_DPAD_LEFT?-1:1;
                    if(a.screen==Screen::Controls){a.controls.edit_option("gain",d*.01f);break;}
                    if(a.screen==Screen::Options&&a.options.focus<(int)a.options.options.size()){a.options.change(a.options.focus,d);break;}
                }
                switch (e.cbutton.button) {
                case SDL_CONTROLLER_BUTTON_DPAD_UP: nav = -1; break;
                case SDL_CONTROLLER_BUTTON_DPAD_DOWN: nav = 1; break;
                case SDL_CONTROLLER_BUTTON_A: case SDL_CONTROLLER_BUTTON_START: enter = true; break;
                case SDL_CONTROLLER_BUTTON_B: back = true; break;
                }
                a.mx = a.my = -1;
                break;
            case SDL_DROPFILE: {  // drag a disc image onto the window
                std::string p = e.drop.file;
                SDL_free(e.drop.file);
                DiscCheck d = check_disc(a.paths, p);
                if (d.ok) { a.settings.iso = p; a.settings.save(); a.status = "Disc verified: Geist (USA)."; a.status_ok = true; }
                else show_message(a, "That disc can't be used", d.error);
                break;
            }
            }
        }
        Uint32 now = SDL_GetTicks();
        a.dt = std::min(0.1f, (now - last) / 1000.0f);
        a.t += a.dt;
        last = now;
        SDL_SetRenderDrawColor(a.r, 0, 0, 0, 255);
        SDL_RenderClear(a.r);
        frame(a, click, nav, enter, back);
        SDL_RenderPresent(a.r);
    }
    if (a.building) { a.cancel = true; }
    if (a.worker.joinable()) a.worker.join();
    SDL_Quit();
    return 0;
}
