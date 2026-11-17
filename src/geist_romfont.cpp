// Geist's menu and HUD text uses the GameCube's system font, which the SDK's OSInitFont
// reads from the console's boot ROM (IPL) at 0x1FCF00. No Nintendo ROM ships with the port,
// so without this every string draws from an empty font: solid grey blocks.
//
// Geist's disc carries a font in exactly the decoded OSFont format (Fonts/default.bin:
// ANSI, 0x20-0xFF, 24x24 cells, one 512x512 I4 sheet -- 131360 bytes, OS_FONT_SIZE_ANSI).
// This rebuilds a ROM font image from the player's own disc at run time: the same header and
// width table, the sheet reduced to the ROM's 2 bits per texel (levels 0, 5, A, F, which the
// header's c0..c3 expand back), Yay0-compressed as OSInitFont expects, placed at the ANSI
// font's ROM address. The SDK's own font code then runs unchanged. Nothing from Nintendo is
// distributed: the bytes come from the disc the player supplies.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "runtime.h"

namespace {

const uint32_t kRomFontAnsi = 0x1FCF00;   // where OSInitFont reads the Windows-1252 font
const uint32_t kRomFontAnsiSize = 0x3000; // and how much it reads
const uint32_t kOsFontSizeAnsi = 0x20120; // a decoded ANSI font: header, widths, I4 sheet

uint32_t be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
uint16_t be16(const uint8_t* p) { return (uint16_t)(p[0] << 8 | p[1]); }
void put32(std::vector<uint8_t>& v, uint32_t x) { for (int i = 3; i >= 0; i--) v.push_back((uint8_t)(x >> (8 * i))); }

// A file on the disc by its FST path (case-insensitive, '/'-separated).
bool disc_file(const char* path, std::vector<uint8_t>& out) {
    uint8_t hdr[0x430];
    if (!iso_read(0, hdr, sizeof(hdr))) return false;
    const uint32_t fst_off = be32(hdr + 0x424), fst_size = be32(hdr + 0x428);
    if (!fst_off || fst_size < 12 || fst_size > (16u << 20)) return false;
    std::vector<uint8_t> fst(fst_size);
    if (!iso_read(fst_off, fst.data(), fst_size)) return false;
    const uint32_t n = be32(fst.data() + 8);
    if ((uint64_t)n * 12 > fst_size) return false;
    const char* names = (const char*)fst.data() + n * 12;
    const size_t names_len = fst_size - n * 12;
    auto name_of = [&](uint32_t i) -> std::string {
        uint32_t o = be32(fst.data() + i * 12) & 0xFFFFFF;
        if (o >= names_len) return {};
        return std::string(names + o, strnlen(names + o, names_len - o));
    };
    auto ieq = [](const std::string& a, const std::string& b) {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); i++)
            if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) return false;
        return true;
    };
    // Walk the path one component at a time within the directory's entry range.
    std::string p = path;
    uint32_t lo = 1, hi = n;
    while (!p.empty()) {
        size_t slash = p.find('/');
        std::string part = p.substr(0, slash);
        p = slash == std::string::npos ? "" : p.substr(slash + 1);
        bool found = false;
        for (uint32_t i = lo; i < hi;) {
            const uint8_t* e = fst.data() + i * 12;
            const bool dir = e[0] != 0;
            if (ieq(name_of(i), part)) {
                if (dir && !p.empty()) { lo = i + 1; hi = be32(e + 8); found = true; break; }
                if (!dir && p.empty()) {
                    out.resize(be32(e + 8));
                    return iso_read(be32(e + 4), out.data(), (uint32_t)out.size());
                }
                return false;
            }
            i = dir ? be32(e + 8) : i + 1;  // skip a whole subdirectory
        }
        if (!found) return false;
    }
    return false;
}

// Yay0, as the SDK's decoder reads it: a bit mask (1 = copy one byte from the chunk table),
// 16-bit links (length-2 in the top nibble, distance-1 below; a zero nibble takes the
// length - 0x12 from the chunk table), then the chunk bytes.
std::vector<uint8_t> yay0(const std::vector<uint8_t>& src) {
    const size_t n = src.size();
    std::vector<uint32_t> masks;
    std::vector<uint8_t> links, chunks;
    uint32_t mask = 0;
    int bits = 0;
    auto bit = [&](bool literal) {
        mask = (mask << 1) | (literal ? 1u : 0u);
        if (++bits == 32) { masks.push_back(mask); mask = 0; bits = 0; }
    };
    // Hash chains on 3-byte prefixes, 4 KB window.
    std::vector<int32_t> head(1 << 16, -1), prev(n, -1);
    auto h3 = [&](size_t i) { return (uint32_t)((src[i] << 8) ^ (src[i + 1] << 4) ^ src[i + 2]) & 0xFFFF; };
    auto insert = [&](size_t i) {
        if (i + 3 > n) return;
        uint32_t h = h3(i);
        prev[i] = head[h];
        head[h] = (int32_t)i;
    };
    size_t i = 0;
    while (i < n) {
        size_t best = 0, dist = 0;
        if (i + 3 <= n) {
            int tries = 256;
            for (int32_t j = head[h3(i)]; j >= 0 && tries-- > 0; j = prev[j]) {
                size_t d = i - (size_t)j;
                if (d > 0x1000) break;
                size_t l = 0, mx = std::min<size_t>(0x111, n - i);
                while (l < mx && src[(size_t)j + l] == src[i + l]) l++;
                if (l > best) { best = l; dist = d; if (l == mx) break; }
            }
        }
        if (best >= 3) {
            bit(false);
            const uint32_t d = (uint32_t)dist - 1;
            if (best <= 0x11) {
                uint16_t v = (uint16_t)(((best - 2) << 12) | d);
                links.push_back(v >> 8); links.push_back(v & 0xFF);
            } else {
                links.push_back((uint8_t)(d >> 8)); links.push_back((uint8_t)d);
                chunks.push_back((uint8_t)(best - 0x12));
            }
            for (size_t k = 0; k < best; k++) insert(i + k);
            i += best;
        } else {
            bit(true);
            chunks.push_back(src[i]);
            insert(i);
            i++;
        }
    }
    if (bits) masks.push_back(mask << (32 - bits));
    std::vector<uint8_t> out = {'Y', 'a', 'y', '0'};
    const uint32_t link_off = 16 + 4 * (uint32_t)masks.size(), chunk_off = link_off + (uint32_t)links.size();
    put32(out, (uint32_t)n);
    put32(out, link_off);
    put32(out, chunk_off);
    for (uint32_t m : masks) put32(out, m);
    out.insert(out.end(), links.begin(), links.end());
    out.insert(out.end(), chunks.begin(), chunks.end());
    return out;
}

bool fill_rom(std::vector<uint8_t>& rom) {
    std::vector<uint8_t> font;
    if (!disc_file("Fonts/default.bin", font) || font.size() != kOsFontSizeAnsi) {
        fprintf(stderr, "[font] Fonts/default.bin not found on the disc; system-font text will not draw\n");
        return false;
    }
    const uint32_t sheet_off = be32(font.data() + 0x24), full = be32(font.data() + 0x28);
    if (be16(font.data()) != 0 || be16(font.data() + 0x16) != 0 /* GX_TF_I4 */ || sheet_off + full > font.size() ||
        font[0x2C] != 0x00 || font[0x2D] != 0x55 || font[0x2E] != 0xAA || font[0x2F] != 0xFF) {
        fprintf(stderr, "[font] Fonts/default.bin is not the expected ANSI I4 font\n");
        return false;
    }
    // Header and width table as they are; the sheet at 2 bits per texel, half the bytes.
    std::vector<uint8_t> img(font.begin(), font.begin() + sheet_off);
    auto q = [](uint8_t nib) { return (uint8_t)((nib * 3 + 7) / 15); };  // nearest of 0,5,A,F
    for (uint32_t k = 0; k < full; k += 2) {
        const uint8_t a = font[sheet_off + k], b = font[sheet_off + k + 1];
        img.push_back((uint8_t)(q(a >> 4) << 6 | q(a & 15) << 4 | q(b >> 4) << 2 | q(b & 15)));
    }
    std::vector<uint8_t> packed = yay0(img);
    if (packed.size() > kRomFontAnsiSize) {
        fprintf(stderr, "[font] rebuilt font is %zu bytes, more than the ROM slot's %u\n", packed.size(), kRomFontAnsiSize);
        return false;
    }
    memcpy(rom.data() + kRomFontAnsi, packed.data(), packed.size());
    fprintf(stderr, "[font] system font rebuilt from the disc (%zu bytes compressed)\n", packed.size());
    return true;
}

struct Register {
    Register() { ipl_set_rom_filler(fill_rom); }
} g_register;

}  // namespace

// Referenced from nothing else: keeps this object in a link that pulls from an archive.
extern "C" int geist_romfont_linked = 1;
