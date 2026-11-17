#include "gci_import.h"
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;
namespace {
constexpr size_t kBlock = 0x2000, kEntry = 0x40, kEntries = 127;
uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] << 8 | p[1]); }
void wr16(uint8_t* p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
// The card's checksums: the sum of the big-endian halfwords, and of their complements.
void checksum(const uint8_t* p, size_t bytes, uint16_t& sum, uint16_t& inv) {
    uint32_t s = 0, i = 0;
    for (size_t k = 0; k < bytes; k += 2) { uint16_t w = rd16(p + k); s += w; i += (uint16_t)~w; }
    sum = (uint16_t)s; inv = (uint16_t)i;
    if (sum == 0xFFFF) sum = 0;
    if (inv == 0xFFFF) inv = 0;
}
bool read_file(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(fs::u8path(path), std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), {});
    return true;
}
}  // namespace

std::string gci_import(const std::string& gci_path, const std::string& card_path) {
    std::vector<uint8_t> gci, card;
    if (!read_file(gci_path, gci)) return "Could not read the .gci file.";
    if (gci.size() < kEntry + kBlock || (gci.size() - kEntry) % kBlock) return "That file is not a GameCube save (.gci).";
    if (!read_file(card_path, card)) return "No memory card yet. Start Geist once so it makes one, then import.";
    if (card.size() < kBlock * 6 || card.size() % kBlock) return "The memory card file looks damaged.";
    const size_t nblocks = card.size() / kBlock;
    const uint16_t need = rd16(&gci[0x38]);
    if (need == 0 || (size_t)need * kBlock != gci.size() - kEntry) return "The .gci file's size does not match its header.";

    // The directory and block map each have two copies; the newer (higher update counter) is current.
    uint8_t* dirs[2] = {&card[kBlock * 1], &card[kBlock * 2]};
    uint8_t* bats[2] = {&card[kBlock * 3], &card[kBlock * 4]};
    const int di = rd16(dirs[1] + 0x1FFA) > rd16(dirs[0] + 0x1FFA) ? 1 : 0;
    const int bi = rd16(bats[1] + 4) > rd16(bats[0] + 4) ? 1 : 0;
    std::vector<uint8_t> dir(dirs[di], dirs[di] + kBlock), bat(bats[bi], bats[bi] + kBlock);
    auto map = [&](size_t b) { return &bat[0x0A + (b - 5) * 2]; };

    // A save with the same game, maker and file name goes first.
    int slot = -1;
    for (size_t e = 0; e < kEntries; e++) {
        uint8_t* d = &dir[e * kEntry];
        if (d[0] == 0xFF) { if (slot < 0) slot = (int)e; continue; }
        if (!memcmp(d, &gci[0], 6) && !memcmp(d + 8, &gci[8], 32)) {
            for (uint16_t b = rd16(d + 0x36), guard = 0; b >= 5 && b < nblocks && guard < nblocks; guard++) {
                const uint16_t next = rd16(map(b));
                wr16(map(b), 0);
                b = next;
            }
            memset(d, 0xFF, kEntry);
            slot = (int)e;
        }
    }
    if (slot < 0) return "The memory card's file list is full.";
    std::vector<uint16_t> freeb;
    for (size_t b = 5; b < nblocks && freeb.size() < need; b++)
        if (rd16(map(b)) == 0) freeb.push_back((uint16_t)b);
    if (freeb.size() < need) return "Not enough free space on the memory card.";

    // Write the save's blocks, chain them, and add its entry.
    for (size_t k = 0; k < need; k++) {
        memcpy(&card[freeb[k] * kBlock], &gci[kEntry + k * kBlock], kBlock);
        wr16(map(freeb[k]), k + 1 < need ? freeb[k + 1] : 0xFFFF);
    }
    memcpy(&dir[slot * kEntry], &gci[0], kEntry);
    wr16(&dir[slot * kEntry + 0x36], freeb[0]);
    uint16_t nfree = 0;
    for (size_t b = 5; b < nblocks; b++) if (rd16(map(b)) == 0) nfree++;
    wr16(&bat[6], nfree);
    wr16(&bat[8], freeb[need - 1]);

    // New update counters, checksums, and both copies made current.
    wr16(&dir[0x1FFA], (uint16_t)(rd16(&dir[0x1FFA]) + 1));
    uint16_t s, i;
    checksum(&dir[0], 0x1FFC, s, i);
    wr16(&dir[0x1FFC], s); wr16(&dir[0x1FFE], i);
    wr16(&bat[4], (uint16_t)(rd16(&bat[4]) + 1));
    checksum(&bat[4], 0x1FFC, s, i);
    wr16(&bat[0], s); wr16(&bat[2], i);
    for (int c = 0; c < 2; c++) { memcpy(dirs[c], dir.data(), kBlock); memcpy(bats[c], bat.data(), kBlock); }

    std::error_code ec;
    fs::copy_file(fs::u8path(card_path), fs::u8path(card_path + ".before-import.raw"), fs::copy_options::overwrite_existing, ec);
    std::ofstream o(fs::u8path(card_path), std::ios::binary | std::ios::trunc);
    if (!o.write((const char*)card.data(), (std::streamsize)card.size())) return "Could not write the memory card.";
    return "";
}
