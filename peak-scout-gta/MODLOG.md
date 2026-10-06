# PEAK Scout in Los Santos: mod log

**Idea:** GTA V Enhanced story mode played as a regular PEAK scout, with PEAK-style climbing on any surface, a stamina bar and GTA's normal weapons. Solo, story mode only.

## Decisions
- Host game: GTA V Enhanced. PEAK is required too: the scout is converted from the player's own PEAK install on first launch, so no game files are shipped.
- Route: our own ASI, loaded by Ultimate ASI Loader (Melty installs v9.7.4). Script Hook V can't be shipped or required, so the mod needs its own native bridge.
- The user accepted the BattlEye/account risk after being told about it, including the GTA Online "altered version" lockout and the withdrawn Los Santos Hunt mashup.

## Melty facts (game_info, 2026-10-05)
- GTA V Enhanced: exe GTA5_Enhanced.exe; asi folder {game}/; BattlEye runs in story mode; never write -nobattleye to commandline.txt.
- Web: script mods reportedly need BattlEye switched off in the Rockstar Games Launcher's story-mode option. Unconfirmed: the probe tests whether an ASI loads with Melty's normal launch.
- one_click_check on the draft recipe (ASI + converter setup step): "One click: yes" (placeholder file sizes).

## Next
1. Run the probe on the user's PC: is PeakScoutGTA.log written after Play from Melty?
2. Native bridge research in GTA5_Enhanced.exe.
3. PEAK scout asset names; how Enhanced loads a custom ped (OpenRPF? licence).

Preflight: `python3 -I tools/preflight.py`

## Melty draft
- modId 0cfe1fea-b000-4eb6-85e0-6f44e20cee67 (draft, private). Release 0.0.1 = probe only (releaseId 9c30df45-97e5-4e13-8aed-7d65902d0f3b), status draft, one_click_check: yes.
- Recipe: gta-v-enhanced primary, peak secondary, loader ultimate-asi-loader, PeakScoutGTA.asi -> {game}.
- Waiting on: the user's Melty Test, and whether PeakScoutGTA.log appears in the GTA folder.
