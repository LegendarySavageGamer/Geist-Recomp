// Geist's cheats for the F2 menu: what each one is (the code patches that read them are in
// recomp/patches.txt, gcn_cheat_on(<id>)), and that they are for the story only. A read
// from a multiplayer map's folder (Z_*, hunt_*) switches every cheat off and keeps them off
// until a story level (01A_X ... 05A_X) loads again.
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include "cheats.h"
#include "runtime.h"

namespace {

// The order fixes the ids patches.txt uses.
const int kHealth = gcn_cheat_register("Infinite Health", "health");  // 0
const int kAmmo = gcn_cheat_register("Infinite Ammo", "ammo");        // 1
const int kBoost = gcn_cheat_register("Infinite Boost", "boost");     // 2
const int kTime = gcn_cheat_register("Infinite Time", "time");        // 3

enum Kind { Other, Story, Multiplayer };
struct Span { uint64_t start, end; Kind kind; };
std::vector<Span> g_spans;
bool g_loaded = false;

uint32_t be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

Kind kind_of(const std::string& top) {
    std::string t = top;
    for (auto& ch : t) ch = (char)tolower((unsigned char)ch);
    if (t.rfind("z_", 0) == 0 || t.rfind("hunt_", 0) == 0) return Multiplayer;
    if (t.size() >= 3 && t[0] == '0' && t[1] >= '1' && t[1] <= '5') return Story;
    return Other;
}

// Where each file lies on the disc, tagged with the top-level folder it is in.
void load_fst() {
    g_loaded = true;
    uint8_t hdr[0x430];
    if (!iso_read(0, hdr, sizeof hdr)) return;
    const uint32_t fst_off = be32(hdr + 0x424), fst_size = be32(hdr + 0x428);
    if (!fst_size || fst_size > (16u << 20)) return;
    std::vector<uint8_t> fst(fst_size);
    if (!iso_read(fst_off, fst.data(), fst_size)) return;
    const uint32_t n = be32(&fst[8]);
    if ((uint64_t)n * 12 > fst_size) return;
    const char* names = (const char*)&fst[n * 12];
    // For each entry, the top-level folder it belongs to (a folder's entry gives the index
    // just past its last child).
    std::vector<int> top(n, -1);
    for (uint32_t i = 1; i < n; i++) {
        const uint8_t* e = &fst[i * 12];
        const bool dir = e[0] != 0;
        if (dir && be32(e + 4) == 0) {  // a child of the root
            for (uint32_t k = i; k < be32(e + 8) && k < n; k++) top[k] = (int)i;
        }
        if (!dir && top[i] >= 0) {
            const std::string folder = names + (be32(&fst[top[i] * 12]) & 0xFFFFFF);
            const Kind k = kind_of(folder);
            if (k != Other) g_spans.push_back({be32(e + 4), (uint64_t)be32(e + 4) + be32(e + 8), k});
        }
    }
}

void on_read(unsigned long long off, unsigned) {
    if (!g_loaded) load_fst();
    for (const Span& s : g_spans)
        if (off >= s.start && off < s.end) {
            if (s.kind == Multiplayer) gcn_cheats_set_allowed(false, "Cheats are off in multiplayer.");
            else gcn_cheats_set_allowed(true, "");
            return;
        }
}

const bool g_hooked = (gcn_dvd_read_hook = on_read, true);

}  // namespace
