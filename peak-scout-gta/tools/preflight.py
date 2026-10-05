"""Preflight: overlay every sheet, list unfilled/unverified cells and unresolved cross-sheet references."""
import json, pathlib, sys

root = pathlib.Path(__file__).resolve().parent.parent / "sheets"
sheets = {p.stem: json.loads(p.read_text()) for p in sorted(root.glob("*.json"))}
ids = {name: {r["id"] for r in s["rows"]} for name, s in sheets.items()}
problems = []

for name, s in sheets.items():
    for row in s["rows"]:
        for col in s["columns"]:
            v = row.get(col)
            if v is None or v == "":
                problems.append(f"{name}.{row['id']}.{col}: empty")
            elif isinstance(v, str) and v.startswith("unverified"):
                problems.append(f"{name}.{row['id']}.{col}: {v}")

for row in sheets["systems"]["rows"]:
    for h in row["depends_on_hooks"]:
        if h not in ids["hooks"]:
            problems.append(f"systems.{row['id']}.depends_on_hooks: '{h}' not in hooks sheet")
        else:
            hrow = next(r for r in sheets["hooks"]["rows"] if r["id"] == h)
            if not hrow["status"].startswith("verified"):
                problems.append(f"systems.{row['id']} -> hooks.{h}: not verified")

print("\n".join(problems) if problems else "preflight clean")
print(f"\n{len(problems)} open cells/references")
sys.exit(1 if problems else 0)
