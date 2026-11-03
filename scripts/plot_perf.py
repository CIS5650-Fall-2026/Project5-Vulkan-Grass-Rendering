"""make the README charts from data/summary.csv (run summarize_perf.py first).

  img/readme/perf_blades.png   frame time vs blade count, culling on vs off (default view)
  img/readme/perf_culling.png  frame time per culling config at 65536 blades (default view)
  img/readme/perf_views.png    the same five configs at 65536 blades from three camera views

usage: python scripts/plot_perf.py
"""
import csv
import os

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

here = os.path.dirname(os.path.abspath(__file__))
data = os.path.join(here, "..", "data", "summary.csv")
img = os.path.join(here, "..", "img", "readme")

SURFACE = "#fcfcfb"
TEXT = "#0b0b0b"
TEXT2 = "#52514e"
GRID = "#e6e5e1"
# categorical slots, fixed order
BLUE, ORANGE, AQUA, YELLOW, MAGENTA = "#2a78d6", "#eb6834", "#1baf7a", "#eda100", "#e87ba4"

with open(data, newline="") as f:
    rows = list(csv.DictReader(f))
for r in rows:
    r["num_blades"] = int(r["num_blades"])
    r["mean_ms"] = float(r["mean_ms"])
    r["std_ms"] = float(r["std_ms"])
    r["drawn_frac"] = float(r["drawn_frac"])

default = [r for r in rows if r["view"] == "default"]


def style(ax):
    ax.set_facecolor(SURFACE)
    for side in ("top", "right"):
        ax.spines[side].set_visible(False)
    for side in ("left", "bottom"):
        ax.spines[side].set_color(GRID)
    ax.tick_params(colors=TEXT2, labelsize=9)
    ax.yaxis.grid(True, color=GRID, linewidth=0.8)
    ax.set_axisbelow(True)


# ---- chart 1: ms vs blade count --------------------------------------------
series = {
    "no_cull":  ("no culling",  BLUE),
    "all_cull": ("all culling", ORANGE),
}
fig, ax = plt.subplots(figsize=(7, 4.2), facecolor=SURFACE)
style(ax)
for key, (name, color) in series.items():
    pts = sorted((r["num_blades"], r["mean_ms"], r["std_ms"]) for r in default if r["label"] == key)
    xs, ys, es = zip(*pts)
    ax.errorbar(xs, ys, yerr=es, color=color, linewidth=2, marker="o", markersize=6,
                markerfacecolor=SURFACE, markeredgewidth=2, capsize=3, label=name)
    ax.annotate(f"{name}  {ys[-1]:.1f} ms", (xs[-1], ys[-1]), xytext=(8, 0),
                textcoords="offset points", va="center", fontsize=9, color=TEXT)
ax.set_xscale("log", base=2)
ax.set_yscale("log")
ax.set_xticks([4096, 16384, 65536, 262144, 1048576])
ax.set_xticklabels(["4K", "16K", "64K", "256K", "1M"])
ax.set_yticks([0.1, 0.3, 1, 3, 10, 30])
ax.set_yticklabels(["0.1", "0.3", "1", "3", "10", "30"])
ax.set_xlabel("blades", color=TEXT2)
ax.set_ylabel("frame time (ms, mean of per-run medians)", color=TEXT2)
ax.set_title("Frame time vs number of blades (default view)", loc="left", color=TEXT, fontsize=11)
ax.legend(frameon=False, loc="upper left", fontsize=9)
ax.set_xlim(3000, 4_500_000)
fig.tight_layout()
fig.savefig(os.path.join(img, "perf_blades.png"), dpi=150, facecolor=SURFACE)

# ---- chart 2: culling configs at 65536, default view -----------------------
labels = {
    "no_cull":      "none",
    "frustum_only": "frustum only",
    "orient_only":  "orientation only",
    "dist_only":    "distance only",
    "all_cull":     "all three",
}
at = {r["label"]: r for r in default if r["num_blades"] == 65536}
order = sorted(labels, key=lambda k: -at[k]["mean_ms"])
base = at["no_cull"]["mean_ms"]

fig, ax = plt.subplots(figsize=(7, 3.6), facecolor=SURFACE)
style(ax)
ax.xaxis.grid(True, color=GRID, linewidth=0.8)
ax.yaxis.grid(False)
ys = range(len(order))
ax.barh(ys, [at[k]["mean_ms"] for k in order], xerr=[at[k]["std_ms"] for k in order],
        color=BLUE, height=0.55, error_kw={"ecolor": TEXT2, "capsize": 3})
for y, k in zip(ys, order):
    ms, drawn = at[k]["mean_ms"], at[k]["drawn_frac"]
    cut = "" if k == "no_cull" else f"  -{(1 - ms / base) * 100:.0f}%"
    ax.text(ms + at[k]["std_ms"] + 0.04, y, f"{ms:.2f} ms{cut}  ({drawn * 100:.0f}% drawn)",
            va="center", fontsize=9, color=TEXT)
ax.set_yticks(list(ys))
ax.set_yticklabels([labels[k] for k in order])
ax.invert_yaxis()
ax.set_xlim(0, base * 1.55)
ax.set_xlabel("frame time (ms, mean of per-run medians), 65 536 blades, default view", color=TEXT2)
ax.set_title("Effect of each culling test", loc="left", color=TEXT, fontsize=11)
fig.tight_layout()
fig.savefig(os.path.join(img, "perf_culling.png"), dpi=150, facecolor=SURFACE)

# ---- chart 3: same five configs from three views ---------------------------
views = ["default", "high", "close"]
cfg_order = ["no_cull", "frustum_only", "orient_only", "dist_only", "all_cull"]
colors = [BLUE, ORANGE, AQUA, YELLOW, MAGENTA]
by = {(r["view"], r["label"]): r for r in rows if r["num_blades"] == 65536}

fig, ax = plt.subplots(figsize=(7, 4), facecolor=SURFACE)
style(ax)
width = 0.15
for i, (k, c) in enumerate(zip(cfg_order, colors)):
    xs = [v + (i - 2) * width for v in range(len(views))]
    vals = [by[(v, k)]["mean_ms"] for v in views]
    errs = [by[(v, k)]["std_ms"] for v in views]
    ax.bar(xs, vals, width=width * 0.9, color=c, label=labels[k],
           yerr=errs, error_kw={"ecolor": TEXT2, "capsize": 2})
ax.set_xticks(range(len(views)))
ax.set_xticklabels(["default (r=10)", "high (phi=-40, r=16)", "close (r=3)"])
ax.set_ylabel("frame time (ms, mean of per-run medians)", color=TEXT2)
ax.set_title("Culling by camera view, 65 536 blades", loc="left", color=TEXT, fontsize=11)
# headroom so the legend sits above the bars instead of on them
ax.set_ylim(0, max(r["mean_ms"] for r in by.values()) * 1.4)
ax.legend(frameon=False, fontsize=9, ncol=5, loc="upper center", handlelength=1.2, columnspacing=1.2)
fig.tight_layout()
fig.savefig(os.path.join(img, "perf_views.png"), dpi=150, facecolor=SURFACE)

# ---- chart 4: tessellation lod on vs off (only if the lod set has been run) --
lod_series = (("lod_off", "fixed 8 x 8", BLUE), ("lod_width", "1 across, 8 along", AQUA), ("lod_on", "1 across, 8 -> 2 along by distance", ORANGE))
lod = {(r["view"], r["label"], r["num_blades"]): r for r in rows if r["label"] in {k for k, _, _ in lod_series}}
if lod:
    lod_views = ["default", "high"]
    counts = sorted({k[2] for k in lod})
    fig, axes = plt.subplots(1, 2, figsize=(9, 3.8), facecolor=SURFACE, sharey=True)
    for ax, view in zip(axes, lod_views):
        style(ax)
        for i, (key, name, c) in enumerate(lod_series):
            xs = [j + (i - 1) * 0.27 for j in range(len(counts))]
            vals = [lod[(view, key, n)]["mean_ms"] for n in counts]
            errs = [lod[(view, key, n)]["std_ms"] for n in counts]
            ax.bar(xs, vals, width=0.25, color=c, label=name, yerr=errs, error_kw={"ecolor": TEXT2, "capsize": 2})
            if key != "lod_off":
                # saving relative to lod 0 (8x8 requested)
                for x, n, v in zip(xs, counts, vals):
                    base_ms = lod[(view, "lod_off", n)]["mean_ms"]
                    ax.text(x, v + lod[(view, key, n)]["std_ms"], f"{(v / base_ms - 1) * 100:+.0f}%",
                            ha="center", va="bottom", fontsize=7, color=TEXT)
        ax.set_xticks(range(len(counts)))
        ax.set_xticklabels([f"{n // 1024}K" if n < 1 << 20 else "1M" for n in counts])
        ax.set_title(view + " view", loc="left", color=TEXT, fontsize=10)
    axes[0].set_ylabel("frame time (ms, mean of per-run medians)", color=TEXT2)
    axes[0].legend(frameon=False, fontsize=8, loc="upper left")
    fig.suptitle("Tessellation levels, all culling on", x=0.02, ha="left", color=TEXT, fontsize=11)
    fig.tight_layout()
    fig.savefig(os.path.join(img, "perf_lod.png"), dpi=150, facecolor=SURFACE)
    print("wrote img/readme/perf_lod.png")

print("wrote img/readme/perf_blades.png, img/readme/perf_culling.png, img/readme/perf_views.png")
