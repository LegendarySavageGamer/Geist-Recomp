// Geist Recompiled builder: turns the player's own legally dumped disc into Geist.exe on
// their PC. The launcher ships no game code; everything derived from the disc is made here,
// in work/ and game/, and never leaves the player's machine.
#pragma once
#include <atomic>
#include <functional>
#include <string>

struct BuildPaths {
    std::string root;       // the launcher's folder
    std::string sdk;        // root/sdk
    std::string toolchain;  // root/toolchain (llvm-mingw)
    std::string python;     // root/python/python.exe
    std::string work;       // root/work  (DOL, generated C, objects: local only)
    std::string game;       // root/game  (Geist.exe and what it needs)
    static BuildPaths from_root(const std::string& root);
};

struct BuildProgress {
    int stage = 0;          // 1 verify, 2 recompile, 3 compile, 4 link, 5 finish
    int stages = 5;
    float fraction = 0;     // overall 0..1
    std::string message;
};

struct DiscCheck {
    bool ok = false;
    std::string id;         // e.g. GITE01
    std::string dol_sha1;
    std::string error;      // what to tell the player when !ok
};

// Reads the disc header and main.dol (plain .iso/.gcm or .ciso) and checks them against the
// SDK manifest. Does not write anything.
DiscCheck check_disc(const BuildPaths& p, const std::string& iso);

// True when game/Geist.exe was built by this SDK version from this disc.
bool game_is_built(const BuildPaths& p, const std::string& iso);

// Full build. progress() is called from the building thread; cancel may be set to stop.
// Returns true on success; on failure `error` says why (details in work/build.log).
bool build_game(const BuildPaths& p, const std::string& iso, std::function<void(const BuildProgress&)> progress,
                std::atomic<bool>& cancel, std::string& error);

// Starts game/Geist.exe with the disc; false if it could not be started.
bool launch_game(const BuildPaths& p, const std::string& iso);

std::string manifest_get(const BuildPaths& p, const std::string& key, const std::string& fallback = "");
std::string sha1_hex(const void* data, size_t len);
