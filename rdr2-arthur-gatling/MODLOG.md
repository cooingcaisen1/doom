# MODLOG - Arthur's Gatling (RDR2)

- Game: Red Dead Redemption 2, RAGE engine, Story Mode only. Melty installs Ultimate ASI Loader v9.7.4 by itself; Script Hook RDR2 can't be bundled, so it isn't used.
- Route: a self-contained `.asi` that runs game natives itself. It finds the game's resolver, script thread and allocator by pattern (from emcifuntik/rdr2scripthook, MIT, targeting build 1.0.1491.50), registers a script thread of its own, and ticks it from a detour on the game's script update.
- The `isInSession` pattern is `cmp byte [rip+disp32], imm8`, so the address is relative to the end of the 7-byte instruction. Upstream's GetOffset(2) reads one byte early. Ours (rip_imm8) is checked by the stand-in test. It's only logged; the guard uses NETWORK_IS_SESSION_STARTED / NETWORK_IS_GAME_IN_PROGRESS.
- Compiler: MinGW-w64 g++ 13. The script-thread vtable is built by hand in MSVC slot order (dtor, Reset, Run, Update, Kill), because g++'s two virtual-dtor slots would shift the layout.
- Gatling prop: the game has no stand-alone Gatling barrel model. `gatling_gun` (the wheeled field gun) is spawned as a vehicle, wheels 0 and 1 are broken off, and it's attached to PH_R_Hand. How it looks has to be tuned in game (live_checks.json).
- Bullets: SHOOT_SINGLE_BULLET_BETWEEN_COORDS with WEAPON_TURRET_GATLING, falling back to WEAPON_REPEATER_CARBINE if that hash is invalid.
- Tested: tests/run_tests.sh, a stand-in RDR2.exe under Wine 9. 35/35 checks pass (patterns land on exact sites, thread registers after the loading screen, F7, spin-up, sprint lock, online guard, auto put-away, every native on our thread).
- Not yet tested: the real game. See sheets/live_checks.json.
