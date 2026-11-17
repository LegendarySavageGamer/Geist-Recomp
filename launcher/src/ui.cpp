// See ui.h.
#include "ui.h"
#include <algorithm>
#include <cmath>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

bool Font::load(SDL_Renderer* r, const std::vector<unsigned char>& ttf, float px, float letter_spacing) {
    px_ = px;
    spacing_ = letter_spacing;
    stbtt_fontinfo info;
    if (!stbtt_InitFont(&info, ttf.data(), stbtt_GetFontOffsetForIndex(ttf.data(), 0))) return false;
    int asc, desc, gap;
    stbtt_GetFontVMetrics(&info, &asc, &desc, &gap);
    ascent_ = asc * stbtt_ScaleForPixelHeight(&info, px);
    tw_ = 1024;
    th_ = 64;
    std::vector<unsigned char> bmp;
    stbtt_bakedchar bc[96];
    for (;;) {  // grow the atlas until everything fits
        bmp.assign((size_t)tw_ * th_, 0);
        if (stbtt_BakeFontBitmap(ttf.data(), 0, px, bmp.data(), tw_, th_, 32, 96, bc) > 0) break;
        th_ *= 2;
        if (th_ > 4096) return false;
    }
    std::vector<Uint32> rgba((size_t)tw_ * th_);
    for (size_t i = 0; i < rgba.size(); i++) rgba[i] = (Uint32)bmp[i] << 24 | 0x00FFFFFF;
    tex_ = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, tw_, th_);
    SDL_UpdateTexture(tex_, nullptr, rgba.data(), tw_ * 4);
    SDL_SetTextureBlendMode(tex_, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(tex_, SDL_ScaleModeLinear);
    for (int i = 0; i < 96; i++) g_[i] = {(float)bc[i].x0, (float)bc[i].y0, (float)bc[i].x1, (float)bc[i].y1, bc[i].xoff, bc[i].yoff, bc[i].xadvance};
    return true;
}

float Font::draw(SDL_Renderer* r, const std::string& text, float x, float y, Color c) const {
    if (!tex_) return 0;
    SDL_SetTextureColorMod(tex_, c.r, c.g, c.b);
    SDL_SetTextureAlphaMod(tex_, c.a);
    float x0 = x;
    for (unsigned char ch : text) {
        if (ch < 32 || ch > 127) ch = '?';
        const Glyph& g = g_[ch - 32];
        SDL_Rect src = {(int)g.x0, (int)g.y0, (int)(g.x1 - g.x0), (int)(g.y1 - g.y0)};
        SDL_FRect dst = {std::round(x + g.xoff), std::round(y + g.yoff), g.x1 - g.x0, g.y1 - g.y0};
        SDL_RenderCopyF(r, tex_, &src, &dst);
        x += g.xadv + spacing_;
    }
    return x - x0;
}

float Font::width(const std::string& text) const {
    float w = 0;
    for (unsigned char ch : text) {
        if (ch < 32 || ch > 127) ch = '?';
        w += g_[ch - 32].xadv + spacing_;
    }
    return text.empty() ? 0 : w - spacing_;
}

namespace ui {
static SDL_Texture* g_glow;
static SDL_Texture* g_white;

void init(SDL_Renderer* r) {
    const int n = 128;
    std::vector<Uint32> px(n * n);
    for (int y = 0; y < n; y++)
        for (int x = 0; x < n; x++) {
            float dx = (x + 0.5f) / n * 2 - 1, dy = (y + 0.5f) / n * 2 - 1;
            float d = std::sqrt(dx * dx + dy * dy);
            float a = std::max(0.0f, 1.0f - d);
            a = a * a * (3 - 2 * a);  // smoothstep falloff
            px[y * n + x] = (Uint32)(a * 255) << 24 | 0x00FFFFFF;
        }
    g_glow = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, n, n);
    SDL_UpdateTexture(g_glow, nullptr, px.data(), n * 4);
    SDL_SetTextureBlendMode(g_glow, SDL_BLENDMODE_ADD);
    Uint32 w = 0xFFFFFFFF;
    g_white = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, 1, 1);
    SDL_UpdateTexture(g_white, nullptr, &w, 4);
    SDL_SetTextureBlendMode(g_white, SDL_BLENDMODE_BLEND);
}

void glow(SDL_Renderer* r, float x, float y, float radius, Color c) {
    SDL_SetTextureColorMod(g_glow, c.r, c.g, c.b);
    SDL_SetTextureAlphaMod(g_glow, c.a);
    SDL_FRect d = {x - radius, y - radius, radius * 2, radius * 2};
    SDL_RenderCopyF(r, g_glow, nullptr, &d);
}

static SDL_Vertex vtx(float x, float y, Color c) { return {{x, y}, {c.r, c.g, c.b, c.a}, {0.5f, 0.5f}}; }

void line(SDL_Renderer* r, float x0, float y0, float x1, float y1, float w, Color c) {
    float dx = x1 - x0, dy = y1 - y0, len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-3f) return;
    float nx = -dy / len * w * 0.5f, ny = dx / len * w * 0.5f;
    SDL_Vertex v[4] = {vtx(x0 + nx, y0 + ny, c), vtx(x1 + nx, y1 + ny, c), vtx(x1 - nx, y1 - ny, c), vtx(x0 - nx, y0 - ny, c)};
    int idx[6] = {0, 1, 2, 0, 2, 3};
    SDL_RenderGeometry(r, g_white, v, 4, idx, 6);
}

void glow_line(SDL_Renderer* r, float x0, float y0, float x1, float y1, float w, Color c) {
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_ADD);
    SDL_SetTextureBlendMode(g_white, SDL_BLENDMODE_ADD);
    for (int i = 3; i >= 1; i--) line(r, x0, y0, x1, y1, w * (1 + i * 2.2f), {c.r, c.g, c.b, (Uint8)(c.a / (i * 3))});
    line(r, x0, y0, x1, y1, w, c);
    SDL_SetTextureBlendMode(g_white, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
}

void poly(SDL_Renderer* r, const std::vector<SDL_FPoint>& p, Color c) {
    if (p.size() < 3) return;
    std::vector<SDL_Vertex> v;
    for (auto& q : p) v.push_back(vtx(q.x, q.y, c));
    std::vector<int> idx;
    for (size_t i = 1; i + 1 < p.size(); i++) idx.insert(idx.end(), {0, (int)i, (int)i + 1});
    SDL_RenderGeometry(r, g_white, v.data(), (int)v.size(), idx.data(), (int)idx.size());
}

void poly_outline(SDL_Renderer* r, const std::vector<SDL_FPoint>& p, float w, Color c) {
    for (size_t i = 0; i < p.size(); i++) {
        const SDL_FPoint& a = p[i];
        const SDL_FPoint& b = p[(i + 1) % p.size()];
        line(r, a.x, a.y, b.x, b.y, w, c);
    }
}

void rect(SDL_Renderer* r, float x, float y, float w, float h, Color c) {
    poly(r, {{x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}}, c);
}

void vgradient(SDL_Renderer* r, float x, float y, float w, float h, Color t, Color b) {
    SDL_Vertex v[4] = {vtx(x, y, t), vtx(x + w, y, t), vtx(x + w, y + h, b), vtx(x, y + h, b)};
    int idx[6] = {0, 1, 2, 0, 2, 3};
    SDL_RenderGeometry(r, g_white, v, 4, idx, 6);
}

void hgradient(SDL_Renderer* r, float x, float y, float w, float h, Color L, Color R) {
    SDL_Vertex v[4] = {vtx(x, y, L), vtx(x + w, y, R), vtx(x + w, y + h, R), vtx(x, y + h, L)};
    int idx[6] = {0, 1, 2, 0, 2, 3};
    SDL_RenderGeometry(r, g_white, v, 4, idx, 6);
}

void arc(SDL_Renderer* r, float cx, float cy, float rad, float a0, float a1, float w, Color c) {
    const int n = 32;
    for (int i = 0; i < n; i++) {
        float t0 = a0 + (a1 - a0) * i / n, t1 = a0 + (a1 - a0) * (i + 1) / n;
        line(r, cx + std::cos(t0) * rad, cy + std::sin(t0) * rad, cx + std::cos(t1) * rad, cy + std::sin(t1) * rad, w, c);
    }
}

bool point_in(const std::vector<SDL_FPoint>& p, float x, float y) {
    bool in = false;
    for (size_t i = 0, j = p.size() - 1; i < p.size(); j = i++)
        if ((p[i].y > y) != (p[j].y > y) && x < (p[j].x - p[i].x) * (y - p[i].y) / (p[j].y - p[i].y) + p[i].x) in = !in;
    return in;
}
}  // namespace ui
