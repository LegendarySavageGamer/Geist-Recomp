// See builder.h.
#include "builder.h"
#include "proc.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------------------
// SHA-1 (for verifying the player's main.dol against the supported build)
// ---------------------------------------------------------------------------------------
namespace {
struct Sha1 {
    uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
    uint8_t buf[64];
    uint64_t len = 0;
    size_t n = 0;
    static uint32_t rol(uint32_t v, int s) { return (v << s) | (v >> (32 - s)); }
    void block(const uint8_t* p) {
        uint32_t w[80];
        for (int i = 0; i < 16; i++) w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
        for (int i = 16; i < 80; i++) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; i++) {
            uint32_t f, k;
            if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999; }
            else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
            else { f = b ^ c ^ d; k = 0xCA62C1D6; }
            uint32_t t = rol(a, 5) + f + e + k + w[i];
            e = d; d = c; c = rol(b, 30); b = a; a = t;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
    }
    void add(const void* data, size_t len_) {
        const uint8_t* p = (const uint8_t*)data;
        len += len_;
        while (len_) {
            size_t k = std::min(len_, 64 - n);
            memcpy(buf + n, p, k);
            n += k; p += k; len_ -= k;
            if (n == 64) { block(buf); n = 0; }
        }
    }
    std::string hex() {
        uint64_t bits = len * 8;
        uint8_t pad = 0x80;
        add(&pad, 1);
        uint8_t z = 0;
        while (n != 56) add(&z, 1);
        uint8_t l[8];
        for (int i = 0; i < 8; i++) l[i] = (uint8_t)(bits >> (56 - 8 * i));
        add(l, 8);
        char out[41];
        for (int i = 0; i < 5; i++) snprintf(out + 8 * i, 9, "%08x", h[i]);
        return out;
    }
};

uint32_t be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

// A disc image reader: plain images, and CISO (a block map in front of the stored blocks).
struct Disc {
    FILE* f = nullptr;
    uint32_t block = 0;              // CISO block size, 0 for a plain image
    std::vector<int32_t> map;        // disc block -> stored block or -1
    explicit Disc(const std::string& path) {
        f = proc_fopen(path, "rb");
        if (!f) return;
        uint8_t h[0x8000];
        if (fread(h, 1, 8, f) == 8 && !memcmp(h, "CISO", 4)) {
            fseek(f, 0, SEEK_SET);
            if (fread(h, 1, sizeof(h), f) != sizeof(h)) { fclose(f); f = nullptr; return; }
            block = h[4] | h[5] << 8 | h[6] << 16 | (uint32_t)h[7] << 24;
            int32_t next = 0;
            for (size_t i = 8; i < sizeof(h); i++) map.push_back(h[i] ? next++ : -1);
        }
    }
    ~Disc() { if (f) fclose(f); }
    bool read(uint64_t off, void* dst, size_t len) {
        uint8_t* p = (uint8_t*)dst;
        while (len) {
            uint64_t at = off;
            size_t n = len;
            if (block) {
                uint64_t b = off / block;
                uint32_t o = (uint32_t)(off % block);
                n = std::min<size_t>(len, block - o);
                if (b >= map.size() || map[b] < 0) { memset(p, 0, n); off += n; p += n; len -= n; continue; }
                at = 0x8000 + (uint64_t)map[b] * block + o;
            }
#ifdef _WIN32
            if (_fseeki64(f, (long long)at, SEEK_SET) != 0) return false;
#else
            if (fseeko(f, (off_t)at, SEEK_SET) != 0) return false;
#endif
            if (fread(p, 1, n, f) != n) return false;
            off += n; p += n; len -= n;
        }
        return true;
    }
};

bool read_dol(Disc& d, std::vector<uint8_t>& dol, std::string& err) {
    uint8_t hdr[0x440];
    if (!d.read(0, hdr, sizeof(hdr))) { err = "Could not read the disc header."; return false; }
    const uint32_t dol_off = be32(hdr + 0x420);
    uint8_t dh[0x100];
    if (!dol_off || !d.read(dol_off, dh, sizeof(dh))) { err = "Could not find the game program on the disc."; return false; }
    uint32_t size = 0;
    for (int i = 0; i < 18; i++) {
        uint32_t off = be32(dh + 4 * i), sz = be32(dh + 0x90 + 4 * i);
        if (sz) size = std::max(size, off + sz);
    }
    if (size < 0x100 || size > 64u << 20) { err = "The game program on the disc looks damaged."; return false; }
    dol.resize(size);
    if (!d.read(dol_off, dol.data(), size)) { err = "Could not read the game program from the disc."; return false; }
    return true;
}

std::string read_text(const std::string& path) {
    std::ifstream f(fs::u8path(path), std::ios::binary);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}

void write_text(const std::string& path, const std::string& text) {
    std::ofstream f(fs::u8path(path), std::ios::binary);
    f << text;
}

std::map<std::string, std::string> parse_ini(const std::string& text) {
    std::map<std::string, std::string> m;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        auto trim = [](std::string s) {
            size_t a = s.find_first_not_of(" \t"), b = s.find_last_not_of(" \t");
            return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
        };
        m[trim(line.substr(0, eq))] = trim(line.substr(eq + 1));
    }
    return m;
}

std::vector<std::string> split_ws(const std::string& s) {
    std::istringstream in(s);
    std::vector<std::string> v;
    std::string w;
    while (in >> w) v.push_back(w);
    return v;
}

std::string join(const std::string& a, const std::string& b) { return (fs::u8path(a) / fs::u8path(b)).u8string(); }

#ifdef _WIN32
const char* kExe = ".exe";
#else
const char* kExe = "";
#endif

std::string tool(const BuildPaths& p, const std::string& name) {
    return join(join(p.toolchain, "bin"), name + kExe);
}

std::string stamp_for(const BuildPaths& p, const std::string& sha) {
    return "sdk=" + manifest_get(p, "sdk_version") + "\ndol_sha1=" + sha + "\n";
}

}  // namespace

std::string sha1_hex(const void* data, size_t len) {
    Sha1 s;
    s.add(data, len);
    return s.hex();
}

BuildPaths BuildPaths::from_root(const std::string& root) {
    BuildPaths p;
    p.root = root;
    p.sdk = join(root, "sdk");
    p.toolchain = join(root, "toolchain");
#ifdef _WIN32
    p.python = join(join(join(p.toolchain, "python"), "bin"), "python.exe");  // llvm-mingw's own Python 3.12
#else
    p.python = "python3";
#endif
    p.work = join(root, "work");
    p.game = join(root, "game");
    // Development overrides (testing the pipeline with a cross toolchain).
    if (const char* e = getenv("GEIST_TOOLCHAIN")) p.toolchain = e;
    if (const char* e = getenv("GEIST_PYTHON")) p.python = e;
    return p;
}

std::string manifest_get(const BuildPaths& p, const std::string& key, const std::string& fallback) {
    static std::mutex m;
    static std::map<std::string, std::map<std::string, std::string>> cache;
    std::lock_guard<std::mutex> lock(m);
    auto& ini = cache[p.sdk];
    if (ini.empty()) ini = parse_ini(read_text(join(p.sdk, "manifest.ini")));
    auto it = ini.find(key);
    return it == ini.end() ? fallback : it->second;
}

DiscCheck check_disc(const BuildPaths& p, const std::string& iso) {
    DiscCheck r;
    const std::string want_id = manifest_get(p, "disc_id"), want_sha = manifest_get(p, "dol_sha1");
    const std::string title = manifest_get(p, "title", "the game");
    if (want_id.empty()) { r.error = "The launcher's sdk folder is missing or damaged. Reinstall Geist Recompiled."; return r; }
    std::string lower = iso;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    for (const char* bad : {".rvz", ".gcz", ".wia", ".nkit.iso"})
        if (lower.size() > strlen(bad) && lower.compare(lower.size() - strlen(bad), strlen(bad), bad) == 0) {
            r.error = "Compressed or trimmed images (.rvz, .gcz, .wia, NKit) are not supported.\n"
                      "Convert it to a full .iso first (Dolphin: right-click the game > Convert File > ISO).";
            return r;
        }
    Disc d(iso);
    if (!d.f) { r.error = "Could not open that file."; return r; }
    uint8_t id[6];
    if (!d.read(0, id, 6)) { r.error = "That file is not a GameCube disc image."; return r; }
    r.id.assign((const char*)id, 6);
    if (r.id != want_id) {
        bool printable = std::all_of(r.id.begin(), r.id.end(), [](char c) { return c >= 0x20 && c < 0x7F; });
        r.error = "This is not " + title + " (" + want_id + ").\nThe file's disc ID is " +
                  (printable ? "\"" + r.id + "\"" : std::string("not a GameCube ID")) + ".";
        return r;
    }
    std::vector<uint8_t> dol;
    if (!read_dol(d, dol, r.error)) return r;
    r.dol_sha1 = sha1_hex(dol.data(), dol.size());
    if (r.dol_sha1 != want_sha) {
        r.error = "This is " + want_id + ", but a different revision or a modified dump than the one supported "
                  "(" + manifest_get(p, "disc_name", want_id) + ").\nRe-dump your own disc unmodified, or check for a launcher update.";
        return r;
    }
    r.ok = true;
    return r;
}

bool game_is_built(const BuildPaths& p, const std::string& iso) {
    const std::string exe = join(p.game, manifest_get(p, "exe_name", "Geist.exe"));
    std::error_code ec;
    if (!fs::exists(fs::u8path(exe), ec)) return false;
    const std::string stamp = read_text(join(p.game, "build_stamp.txt"));
    // The stamp names the SDK and the DOL; the DOL is checked again before every build only,
    // so a stamp from this SDK with this disc's known hash is enough to play.
    return stamp == stamp_for(p, manifest_get(p, "dol_sha1")) && !iso.empty();
}

bool build_game(const BuildPaths& p, const std::string& iso, std::function<void(const BuildProgress&)> progress,
                std::atomic<bool>& cancel, std::string& error) {
    using clock = std::chrono::steady_clock;
    const auto t0 = clock::now();
    std::error_code ec;
    fs::create_directories(fs::u8path(p.work), ec);
    fs::create_directories(fs::u8path(p.game), ec);
    const std::string log = join(p.work, "build.log");
    { std::ofstream(fs::u8path(log), std::ios::trunc) << "Geist Recompiled builder, SDK " << manifest_get(p, "sdk_version") << "\n"; }
    auto logline = [&](const std::string& s) { std::ofstream(fs::u8path(log), std::ios::app) << s << "\n"; };
    BuildProgress bp;
    auto report = [&](int stage, float within, const std::string& msg) {
        // Stage weights: compile dominates.
        static const float start[] = {0, 0.00f, 0.03f, 0.10f, 0.93f, 0.99f}, span[] = {0, 0.03f, 0.07f, 0.83f, 0.06f, 0.01f};
        bp.stage = stage;
        bp.fraction = std::min(1.0f, start[stage] + span[stage] * std::clamp(within, 0.0f, 1.0f));
        bp.message = msg;
        logline("[" + std::to_string(stage) + "] " + msg);
        if (progress) progress(bp);
    };
    auto fail = [&](const std::string& why) {
        error = why;
        logline("FAILED: " + why);
        return false;
    };

    // 1. Verify the player's disc and write their main.dol to work/.
    report(1, 0, "Checking your disc...");
    DiscCheck dc = check_disc(p, iso);
    if (!dc.ok) return fail(dc.error);
    {
        Disc d(iso);
        std::vector<uint8_t> dol;
        std::string err;
        if (!read_dol(d, dol, err)) return fail(err);
        std::ofstream f(fs::u8path(join(p.work, "main.dol")), std::ios::binary | std::ios::trunc);
        f.write((const char*)dol.data(), (std::streamsize)dol.size());
        if (!f) return fail("Could not write to " + p.work + " (is the drive full or read-only?)");
    }
    report(1, 1, "Disc verified: " + dc.id + ", program " + dc.dol_sha1.substr(0, 12) + "...");
    if (cancel) return fail("Cancelled.");

    // 2. Recompile the PowerPC program into C with the SDK's tables.
    report(2, 0, "Recompiling the game program...");
    const std::string gen = join(p.work, "gen");
    fs::create_directories(fs::u8path(gen), ec);
    {
        std::vector<std::string> cmd = {p.python, join(join(p.sdk, "recomp"), "recomp.py"), join(p.work, "main.dol"),
                                        join(p.sdk, "symbols.txt"), gen, join(p.sdk, "tables")};
        int rc = proc_run(cmd, log, p.work);
        if (rc != 0) return fail("The recompiler failed (code " + std::to_string(rc) + "). See work\\build.log.");
    }
    std::vector<std::string> sources;
    for (auto& e : fs::directory_iterator(fs::u8path(gen), ec))
        if (e.path().extension() == ".c") sources.push_back(e.path().u8string());
    std::sort(sources.begin(), sources.end());
    if (sources.empty()) return fail("The recompiler produced no code. See work\\build.log.");
    report(2, 1, "Recompiled into " + std::to_string(sources.size()) + " source files.");
    if (cancel) return fail("Cancelled.");

    // 3. Compile, in parallel. An object newer than its source from the same SDK is reused.
    const std::string obj = join(p.work, "obj");
    fs::create_directories(fs::u8path(obj), ec);
    const std::string sdk_stamp = join(obj, "sdk_stamp.txt");
    const bool same_sdk = read_text(sdk_stamp) == stamp_for(p, dc.dol_sha1);
    std::vector<std::string> cflags = split_ws(manifest_get(p, "cflags"));
    const std::string cc = tool(p, manifest_get(p, "cc", "x86_64-w64-mingw32-clang"));
    std::vector<std::string> objects(sources.size());
    std::atomic<size_t> next{0}, done{0};
    std::atomic<bool> failed{false};
    std::mutex m;
    std::string first_error;
    unsigned workers = std::max(1u, std::min(16u, std::thread::hardware_concurrency()));
    if (const char* j = getenv("GEIST_JOBS")) workers = std::max(1, atoi(j));
    report(3, 0, "Compiling with " + std::to_string(workers) + " threads...");
    auto worker = [&]() {
        for (;;) {
            size_t i = next++;
            if (i >= sources.size() || failed || cancel) return;
            const fs::path src = fs::u8path(sources[i]);
            const std::string o = join(obj, src.stem().u8string() + ".o");
            objects[i] = o;
            std::error_code e2;
            bool fresh = same_sdk && fs::exists(fs::u8path(o), e2) &&
                         fs::last_write_time(fs::u8path(o), e2) >= fs::last_write_time(src, e2);
            if (!fresh) {
                std::vector<std::string> cmd = {cc};
                cmd.insert(cmd.end(), cflags.begin(), cflags.end());
                cmd.insert(cmd.end(), {"-I" + join(p.sdk, "include"), "-I" + gen, "-c", sources[i], "-o", o});
                const std::string unit_log = o + ".log";
                int rc = proc_run(cmd, unit_log, p.work);
                std::string out = read_text(unit_log);
                {
                    std::lock_guard<std::mutex> l(m);
                    if (!out.empty()) logline(out);
                    if (rc != 0 && !failed) {
                        failed = true;
                        first_error = "Compiling " + src.filename().u8string() + " failed (code " + std::to_string(rc) + ").";
                    }
                }
                fs::remove(fs::u8path(unit_log), e2);
            }
            size_t n = ++done;
            std::lock_guard<std::mutex> l(m);
            report(3, (float)n / sources.size(),
                   "Compiling " + std::to_string(n) + " of " + std::to_string(sources.size()) + (fresh ? " (cached)" : ""));
        }
    };
    {
        std::vector<std::thread> pool;
        for (unsigned i = 0; i < workers; i++) pool.emplace_back(worker);
        for (auto& t : pool) t.join();
    }
    if (cancel) return fail("Cancelled.");
    if (failed) return fail(first_error + " See work\\build.log.");
    write_text(sdk_stamp, stamp_for(p, dc.dol_sha1));

    // 4. Link against the prebuilt runtime (no game code in it) and SDL2.
    report(4, 0, "Linking " + manifest_get(p, "exe_name", "Geist.exe") + "...");
    const std::string exe = join(p.game, manifest_get(p, "exe_name", "Geist.exe"));
    {
        std::vector<std::string> cmd = {tool(p, manifest_get(p, "cxx", "x86_64-w64-mingw32-clang++")), "-o", exe};
        cmd.insert(cmd.end(), objects.begin(), objects.end());
        for (const auto& lib : split_ws(manifest_get(p, "link_inputs"))) cmd.push_back(join(p.sdk, lib));
        for (const auto& f : split_ws(manifest_get(p, "ldflags"))) cmd.push_back(f);
        int rc = proc_run(cmd, log, p.work);
        if (rc != 0) return fail("Linking failed (code " + std::to_string(rc) + "). See work\\build.log.");
    }
    report(4, 1, "Linked.");

    // 5. What the game needs beside it, then the stamp that says it is ready.
    report(5, 0, "Finishing...");
    for (const auto& dll : split_ws(manifest_get(p, "runtime_dlls"))) {
        fs::copy_file(fs::u8path(join(p.toolchain, dll)), fs::u8path(join(p.game, fs::u8path(dll).filename().u8string())),
                      fs::copy_options::overwrite_existing, ec);
        if (ec) return fail("Could not copy " + dll + ": " + ec.message());
    }
    for (const auto& f : split_ws(manifest_get(p, "game_files"))) {  // defaults the player may edit: never overwritten
        const std::string dst = join(p.game, fs::u8path(f).filename().u8string());
        if (!fs::exists(fs::u8path(dst), ec)) fs::copy_file(fs::u8path(join(p.sdk, f)), fs::u8path(dst), ec);
    }
    write_text(join(p.game, "build_stamp.txt"), stamp_for(p, dc.dol_sha1));
    const auto secs = std::chrono::duration_cast<std::chrono::seconds>(clock::now() - t0).count();
    report(5, 1, "Built in " + std::to_string(secs / 60) + " min " + std::to_string(secs % 60) + " s.");
    return true;
}

bool launch_game(const BuildPaths& p, const std::string& iso) {
    const std::string exe = join(p.game, manifest_get(p, "exe_name", "Geist.exe"));
    return proc_spawn_detached({exe, iso}, p.game, join(p.game, "geist_log.txt"));
}
