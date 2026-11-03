"""collapse data/*.csv (written by main.cpp) into data/summary.csv.

each csv is one run. grouped by (label, num_blades, view); for each run the median over its seconds is taken
first, then mean / stdev across the runs. second 0 is dropped because the app hasn't settled yet.

usage: python scripts/summarize_perf.py
"""
import csv
import glob
import os
import statistics
from collections import defaultdict

root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "data")
groups = defaultdict(list)   # (label, num_blades, view) -> list of per-run dicts

for path in sorted(glob.glob(os.path.join(root, "*.csv"))):
    if os.path.basename(path) == "summary.csv":
        continue
    with open(path, newline="") as f:
        data = [r for r in csv.DictReader(f) if int(r["second"]) > 0]
    key = (data[0]["label"], int(data[0]["num_blades"]), data[0]["view"])
    groups[key].append({
        "samples": len(data),
        "ms": statistics.median(float(r["ms"]) for r in data),
        "fps": statistics.median(float(r["fps"]) for r in data),
        "drawn": statistics.median(float(r["drawn"]) for r in data),
    })

rows = []
for (label, n, view), runs in groups.items():
    ms = [r["ms"] for r in runs]
    rows.append({
        "label": label,
        "num_blades": n,
        "view": view,
        "runs": len(runs),
        "samples_per_run": min(r["samples"] for r in runs),
        "mean_ms": round(statistics.mean(ms), 3),
        "std_ms": round(statistics.stdev(ms), 3),
        "mean_fps": round(statistics.mean(r["fps"] for r in runs), 1),
        "drawn_frac": round(statistics.median(r["drawn"] for r in runs) / n, 3),
    })

view_order = {"default": 0, "high": 1, "close": 2}
rows.sort(key=lambda r: (view_order[r["view"]], r["num_blades"], r["label"]))

out = os.path.join(root, "summary.csv")
with open(out, "w", newline="") as f:
    w = csv.DictWriter(f, fieldnames=rows[0].keys())
    w.writeheader()
    w.writerows(rows)

print(f"wrote {out}")
