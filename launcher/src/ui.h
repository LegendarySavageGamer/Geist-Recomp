// Small drawing toolkit for the launcher: TrueType text (stb_truetype), soft glows,
// polygons and thick lines on an SDL_Renderer. Everything is in a 1280x720 logical space.
#pragma once
#include <SDL.h>
#include <string>
#include <vector>

struct Color { Uint8 r, g, b, a; };

class Font {
public:
    bool load(SDL_Renderer* r, const std::vector<unsigned char>& ttf, float px, float letter_spacing = 0);
    // Draws `text` with its left edge at x and its baseline at y. Returns the advance.
    float draw(SDL_Renderer* r, const std::string& text, float x, float y, Color c) const;
    float width(const std::string& text) const;
    float size() const { return px_; }
    float ascent() const { return ascent_; }

private:
    struct Glyph { float x0, y0, x1, y1, xoff, yoff, xadv; };
    SDL_Texture* tex_ = nullptr;
    int tw_ = 0, th_ = 0;
    Glyph g_[96] = {};
    float px_ = 0, spacing_ = 0, ascent_ = 0;
};

namespace ui {
void init(SDL_Renderer* r);
// A soft round glow (additive), radius in pixels.
void glow(SDL_Renderer* r, float x, float y, float radius, Color c);
void line(SDL_Renderer* r, float x0, float y0, float x1, float y1, float width, Color c);
void glow_line(SDL_Renderer* r, float x0, float y0, float x1, float y1, float width, Color c);
// A filled convex polygon.
void poly(SDL_Renderer* r, const std::vector<SDL_FPoint>& pts, Color c);
void poly_outline(SDL_Renderer* r, const std::vector<SDL_FPoint>& pts, float width, Color c);
void rect(SDL_Renderer* r, float x, float y, float w, float h, Color c);
void vgradient(SDL_Renderer* r, float x, float y, float w, float h, Color top, Color bottom);
void hgradient(SDL_Renderer* r, float x, float y, float w, float h, Color left, Color right);
void arc(SDL_Renderer* r, float cx, float cy, float rad, float a0, float a1, float width, Color c);
bool point_in(const std::vector<SDL_FPoint>& pts, float x, float y);
}  // namespace ui
