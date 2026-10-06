# Arthur's Gatling (Red Dead Redemption 2 mod)

Press **F7** in Story Mode and Arthur hauls out a Gatling gun, held at the hip.
Hold **fire** and the barrel spins up into a stream of bullets, with no reloading.
He can't sprint while he's carrying it. Press **F7** again to put it away.

- **Story Mode only.** If an online session starts, the mod puts the Gatling away and does nothing until you're back in Story Mode.
- **Solo**, single player.
- Built on **Ultimate ASI Loader**, so there's no Script Hook RDR2 to install. Melty installs the loader and this one `.asi` file.
- The Gatling is put away automatically if Arthur dies, mounts a horse, gets on a wagon or swims.

## Features
- F7 brings the Gatling out and puts it away
- Hold fire to spray: spin-up from about 6 to 18 rounds a second, with no ammo used
- The game's own Gatling model, bullets, sound and muzzle flash
- No sprinting while the Gatling is out (walking and jogging stay normal)
- Story Mode only: idle in Red Dead Online

## How it is built
The design lives in `sheets/*.json`, which are the source of truth:

| Sheet | What one row is |
|---|---|
| `natives.json` | a game function the mod calls (hash, arguments, return) |
| `game_hooks.json` | a byte pattern in RDR2.exe the mod needs, to run game functions without Script Hook |
| `weapon.json` | the gun: model, grip, muzzle, bullet type, damage, spin-up, pose |
| `controls.json` | an input the mod reads or blocks |
| `systems.json` | a game system the mod touches, and which rows of the other sheets it uses |
| `live_checks.json` | something only the running game can confirm, and its status |

```
python3 -I tools/preflight.py --nativedb <rdr3-nativedb-data>/natives.json   # every cell filled, every reference resolves
./build.sh --nativedb <rdr3-nativedb-data>/natives.json                      # preflight + codegen + MinGW-w64 build -> build/ArthurGatling.asi
tests/run_tests.sh                                                            # runs the .asi inside a stand-in game under Wine
```

`src/generated.h` is generated from the sheets. `src/runtime.cpp` finds the game's
script machinery and ticks a script thread of the mod's own, and `src/gatling.cpp` is the gun.
The log is `ArthurGatling.log` next to the `.asi` in the game folder.

## Credits
- Native-calling and script-thread approach and patterns: [emcifuntik/rdr2scripthook](https://github.com/emcifuntik/rdr2scripthook) (MIT)
- Native signatures: [alloc8or/rdr3-nativedb-data](https://github.com/alloc8or/rdr3-nativedb-data)
- Model, anim and effect names: [femga/rdr3_discoveries](https://github.com/femga/rdr3_discoveries)
- Hooking: [MinHook](https://github.com/TsudaKageyu/minhook) by Tsuda Kageyu (BSD 2-Clause)
- Loader: [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) by ThirteenAG (installed by Melty, not bundled)
- Built with AI assistance (Claude Code). Not affiliated with Rockstar Games.
