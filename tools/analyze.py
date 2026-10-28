#!/usr/bin/env python3
"""Summarize benchmark CSVs and draw dependency-free SVG performance charts."""
import argparse
from collections import defaultdict
import csv
import html
import json
import math
from pathlib import Path
import statistics


COLORS = {"none": "#5a6170", "orientation": "#bb7628", "frustum": "#377dad",
          "distance": "#8c62a2", "all": "#398b53"}


def number(row, name):
    value = float(row[name])
    if not math.isfinite(value):
        raise ValueError(f"Nonfinite {name}: {value}")
    return value


def chart(path, groups, metric, title, ylabel):
    width, height = 900, 480
    left, right, top, bottom = 90, 35, 70, 90
    counts = sorted({key[0] for key in groups})
    maximum = max(values[metric] for values in groups.values()) * 1.12 or 1
    log_min, log_max = math.log2(min(counts) or 1), math.log2(max(counts) or 1)
    def x(n):
        return left + (math.log2(n or 1) - log_min) / max(log_max - log_min, 1) * (width - left - right)
    def y(v):
        return height - bottom - v / maximum * (height - top - bottom)
    parts = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {width} {height}" role="img" aria-label="{html.escape(title)}">',
             '<rect width="100%" height="100%" fill="#fafbf8"/>',
             '<g font-family="Arial,Helvetica,sans-serif" fill="#263129">',
             f'<text x="{left}" y="32" font-size="22" font-weight="bold">{html.escape(title)}</text>']
    for i in range(6):
        value = maximum * i / 5
        yy = y(value)
        parts += [f'<path d="M{left},{yy}H{width-right}" stroke="#d9dfd8"/>',
                  f'<text x="{left-12}" y="{yy+5}" text-anchor="end" font-size="13">{value:.2f}</text>']
    for count in counts:
        parts.append(f'<text x="{x(count)}" y="{height-bottom+26}" text-anchor="middle" font-size="13">{count:,}</text>')
    parts += [f'<text x="{(left+width-right)/2}" y="{height-23}" text-anchor="middle" font-size="14">Generated grass blades (logarithmic scale)</text>',
              f'<text transform="translate(22,{(top+height-bottom)/2}) rotate(-90)" text-anchor="middle" font-size="14">{html.escape(ylabel)}</text>']
    legend_x = left
    for mode, color in COLORS.items():
        points = [(count, groups[(count, mode)][metric]) for count in counts if (count, mode) in groups]
        if not points:
            continue
        coordinates = " ".join(f"{x(count):.2f},{y(value):.2f}" for count, value in points)
        parts.append(f'<polyline points="{coordinates}" fill="none" stroke="{color}" stroke-width="2.5"/>')
        for count, value in points:
            parts.append(f'<circle cx="{x(count):.2f}" cy="{y(value):.2f}" r="4" fill="{color}"/>')
        parts += [f'<path d="M{legend_x},50h18" stroke="{color}" stroke-width="3"/>',
                  f'<text x="{legend_x+24}" y="54" font-size="13">{mode}</text>']
        legend_x += 146
    parts.append('</g></svg>')
    path.write_text("\n".join(parts) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("raw", type=Path)
    parser.add_argument("--output", type=Path, default=Path("performance"))
    args = parser.parse_args()
    manifest = json.loads((args.raw / "manifest.json").read_text())
    args.output.mkdir(parents=True, exist_ok=True)
    metrics = ["gpu_ms", "sim_ms", "cull_ms", "depth_ms", "graphics_tail_ms", "cpu_ms", "visible",
               "orientation_rejected", "frustum_rejected", "distance_rejected", "occlusion_rejected"]
    grouped = defaultdict(list)
    for run in manifest["runs"]:
        if run["returncode"] != 0:
            raise ValueError(f"Incomplete benchmark run: {run['csv']}")
        with (args.raw / run["csv"]).open() as stream:
            rows = list(csv.DictReader(line for line in stream if not line.startswith("#")))
        if len(rows) != manifest["measured_frames"]:
            raise ValueError(f"{run['csv']}: expected {manifest['measured_frames']} frames, got {len(rows)}")
        key = (run["experiment"], run["count"], run["culling"], run["lod"], run["occlusion"], run["camera"])
        grouped[key].append({metric: statistics.median(number(row, metric) for row in rows) for metric in metrics})
    summaries = []
    for key, runs in sorted(grouped.items()):
        row = dict(zip(["experiment", "blade_count", "culling", "lod", "occlusion", "camera"], key))
        row["repetitions"] = len(runs)
        row.update({metric: statistics.median(run[metric] for run in runs) for metric in metrics})
        row["gpu_run_min_ms"] = min(run["gpu_ms"] for run in runs)
        row["gpu_run_max_ms"] = max(run["gpu_ms"] for run in runs)
        summaries.append(row)
    with (args.output / "summary.csv").open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(summaries[0]))
        writer.writeheader()
        writer.writerows(summaries)
    curves = {(row["blade_count"], row["culling"]): row for row in summaries if row["experiment"] == "culling"}
    if curves:
        for (count, mode), row in curves.items():
            row["speedup"] = curves[(count, "none")]["gpu_ms"] / max(row["gpu_ms"], 1e-9)
        chart(args.output / "scaling.svg", curves, "gpu_ms", "Grass rendering: blade-count scaling", "GPU frame span (ms)")
        chart(args.output / "culling-speedup.svg", curves, "speedup", "Culling: improvement over the same unculled scene", "Speedup (no culling / selected culling)")
    print(json.dumps(summaries, indent=2))


if __name__ == "__main__":
    main()
