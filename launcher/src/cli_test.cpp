#include "builder.h"
#include <cstdio>
int main(int argc, char** argv) {
    BuildPaths p = BuildPaths::from_root(argv[1]);
    DiscCheck d = check_disc(p, argv[2]);
    printf("check_disc(%s): ok=%d id=%s sha=%s err=%s\n", argv[2], d.ok, d.id.c_str(), d.dol_sha1.c_str(), d.error.c_str());
    if (argc > 3) return 0;
    std::atomic<bool> cancel{false};
    std::string err;
    int last = -1;
    bool ok = build_game(p, argv[2], [&](const BuildProgress& b) {
        int pct = (int)(b.fraction * 100);
        if (pct / 10 != last / 10 || b.stage != 3) { printf("%3d%%  [%d/5] %s\n", pct, b.stage, b.message.c_str()); fflush(stdout); }
        last = pct;
    }, cancel, err);
    printf(ok ? "BUILD OK, built=%d\n" : "BUILD FAILED: %s\n", ok ? (int)game_is_built(p, argv[2]) : 0, err.c_str());
    return ok ? 0 : 1;
}
