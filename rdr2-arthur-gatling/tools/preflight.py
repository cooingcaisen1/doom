"""Preflight: lay every sheet over the others before a build.

Lists unfilled cells, references between sheets that do not resolve, natives that
disagree with the native database, control hashes that are not joaat(name), and
rows nothing uses. Exit code 1 if anything blocks the build. Live (in-game) checks
that are still pending are listed but do not block a build: they block calling it tested.

usage: python3 -I tools/preflight.py [--nativedb path/to/natives.json]
"""
import json, sys, pathlib, re

ROOT = pathlib.Path(__file__).resolve().parent.parent
SHEETS = ROOT / "sheets"

def joaat(s):
    h = 0
    for c in s.lower().encode():
        h = (h + c) & 0xFFFFFFFF
        h = (h + (h << 10)) & 0xFFFFFFFF
        h ^= h >> 6
    h = (h + (h << 3)) & 0xFFFFFFFF
    h ^= h >> 11
    h = (h + (h << 15)) & 0xFFFFFFFF
    return h

def load(name):
    return json.loads((SHEETS / f"{name}.json").read_text())

def empty(v):
    return v is None or v == "" or (isinstance(v, list) and v is not None and False)

def main():
    args = sys.argv[1:]
    nativedb = None
    if "--nativedb" in args:
        nativedb = json.loads(pathlib.Path(args[args.index("--nativedb") + 1]).read_text())

    sheets = {n: load(n) for n in ["natives", "game_hooks", "weapon", "controls", "systems", "live_checks"]}
    blocking, notes = [], []

    # 1. every row x column crossing is filled
    for name, s in sheets.items():
        for i, row in enumerate(s["rows"]):
            rid = row.get("id", row.get("name", i))
            for col in s["columns"]:
                if col not in row:
                    blocking.append(f"{name}[{rid}].{col}: missing")
                elif row[col] is None or row[col] == "":
                    if not (name == "live_checks" and col == "evidence"):
                        blocking.append(f"{name}[{rid}].{col}: empty")
            for col in row:
                if col not in s["columns"]:
                    blocking.append(f"{name}[{rid}].{col}: column not declared in sheet")

    natives = {r["name"]: r for r in sheets["natives"]["rows"]}
    hooks = {r["id"]: r for r in sheets["game_hooks"]["rows"]}
    controls = {r["id"]: r for r in sheets["controls"]["rows"]}
    guns = {r["id"]: r for r in sheets["weapon"]["rows"]}
    systems = {r["id"]: r for r in sheets["systems"]["rows"]}

    # 2. references between sheets resolve, and every row is used
    used = {"natives": set(), "controls": set(), "hooks": set(), "gun": set()}
    for sid, s in systems.items():
        for n in s["natives"]:
            (used["natives"].add(n) if n in natives else blocking.append(f"systems[{sid}].natives -> {n}: no such native row"))
        for c in s["controls"]:
            (used["controls"].add(c) if c in controls else blocking.append(f"systems[{sid}].controls -> {c}: no such control row"))
        for h in s["hooks"]:
            (used["hooks"].add(h) if h in hooks else blocking.append(f"systems[{sid}].hooks -> {h}: no such hook row"))
        if s["gun"] != "-":
            (used["gun"].add(s["gun"]) if s["gun"] in guns else blocking.append(f"systems[{sid}].gun -> {s['gun']}: no such weapon row"))
    for c in controls.values():
        if c["usedBy"] not in systems:
            blocking.append(f"controls[{c['id']}].usedBy -> {c['usedBy']}: no such system")
    for n in natives:
        if n not in used["natives"]:
            blocking.append(f"natives[{n}]: no system uses it")
    for c in controls:
        if c not in used["controls"]:
            blocking.append(f"controls[{c}]: no system uses it")
    for h in hooks:
        if h not in used["hooks"]:
            blocking.append(f"game_hooks[{h}]: no system uses it")
    for lc in sheets["live_checks"]["rows"]:
        if lc["sheet"] not in sheets:
            blocking.append(f"live_checks[{lc['id']}].sheet -> {lc['sheet']}: no such sheet")
        else:
            ids = {r.get("id", r.get("name")) for r in sheets[lc["sheet"]]["rows"]}
            if lc["row"] != "*" and lc["row"] not in ids:
                blocking.append(f"live_checks[{lc['id']}].row -> {lc['row']}: no such row in {lc['sheet']}")
            if lc["column"] not in sheets[lc["sheet"]]["columns"]:
                blocking.append(f"live_checks[{lc['id']}].column -> {lc['column']}: no such column in {lc['sheet']}")
        if lc["status"] == "pending":
            notes.append(f"live check pending: {lc['id']} - {lc['question']}")

    # 3. values are well formed
    known_types = {"Ped", "Player", "Entity", "Vehicle", "Hash", "BOOL", "int", "float", "const char*", "Vehicle*", "Entity*", "Vector3", "void"}
    for n, r in natives.items():
        if not re.fullmatch(r"0x[0-9A-F]{16}", r["hash"]):
            blocking.append(f"natives[{n}].hash: not a 64-bit hex hash")
        for t in r["args"] + [r["returns"]]:
            if t not in known_types:
                blocking.append(f"natives[{n}]: type '{t}' has no generator mapping")
    for c in controls.values():
        if c["kind"] == "control":
            want = f"0x{joaat(c['name']):08X}"
            if c["code"].upper() != want.upper():
                blocking.append(f"controls[{c['id']}].code {c['code']} != joaat({c['name']}) {want}")
        if c["mode"] not in {"edge", "read", "read_disabled", "disable"}:
            blocking.append(f"controls[{c['id']}].mode: unknown '{c['mode']}'")
    for h in hooks.values():
        if not re.fullmatch(r"([0-9A-F]{2}|\?)( ([0-9A-F]{2}|\?))*", h["pattern"]):
            blocking.append(f"game_hooks[{h['id']}].pattern: malformed")
        if h["resolve"] not in {"direct", "rip", "rip_imm8", "call"}:
            blocking.append(f"game_hooks[{h['id']}].resolve: unknown '{h['resolve']}'")
    for g in guns.values():
        for col in ["attachOffset", "attachRot", "muzzleFallbackOffset"]:
            if not (isinstance(g[col], list) and len(g[col]) == 3):
                blocking.append(f"weapon[{g['id']}].{col}: needs [x,y,z]")
        if g["fireIntervalMinMs"] > g["fireIntervalStartMs"]:
            blocking.append(f"weapon[{g['id']}]: fireIntervalMinMs > fireIntervalStartMs")
        if g["propKind"] != "vehicle":
            blocking.append(f"weapon[{g['id']}].propKind: only 'vehicle' is implemented")

    # 4. natives agree with the native database
    if nativedb:
        flat = {}
        for ns in nativedb.values():
            for h, d in ns.items():
                flat[h.upper().replace("0X", "0x")] = d
        for n, r in natives.items():
            d = flat.get(r["hash"])
            if not d:
                blocking.append(f"natives[{n}].hash {r['hash']}: not in native database")
                continue
            if d["name"] != n:
                blocking.append(f"natives[{n}]: database calls {r['hash']} '{d['name']}'")
            dbargs = [p["type"] for p in d["params"]]
            if dbargs != r["args"]:
                blocking.append(f"natives[{n}].args {r['args']} != database {dbargs}")
            if d["return_type"] != r["returns"]:
                blocking.append(f"natives[{n}].returns {r['returns']} != database {d['return_type']}")
    else:
        notes.append("native database not given: hashes/signatures not cross-checked (pass --nativedb)")

    for line in notes:
        print("NOTE  " + line)
    for line in blocking:
        print("BLOCK " + line)
    total = sum(len(s["rows"]) * len(s["columns"]) for s in sheets.values())
    print(f"preflight: {len(sheets)} sheets, {total} cells, {len(blocking)} blocking, {len(notes)} notes")
    return 1 if blocking else 0

if __name__ == "__main__":
    sys.exit(main())
