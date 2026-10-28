#!/usr/bin/env python3
"""Capture controlled visual comparisons and measure grass interaction recovery."""
import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
import shutil
import statistics
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("--output", type=Path, default=Path("performance/validation/visuals"))
    parser.add_argument("--images", type=Path, default=Path("img"))
    parser.add_argument("--skip-probe", action="store_true")
    args = parser.parse_args()
    exe, output, images = args.executable.resolve(), args.output.resolve(), args.images.resolve()
    output.mkdir(parents=True, exist_ok=True)
    images.mkdir(parents=True, exist_ok=True)
    report = {"cases": []}

    def pixels(name):
        return subprocess.check_output(["ffmpeg", "-v", "error", "-i", str(images / (name + ".png")),
                                        "-f", "rawvideo", "-pix_fmt", "rgba", "-"])

    def difference(first, second):
        left, right = pixels(first), pixels(second)
        if len(left) != 1280 * 720 * 4 or len(right) != len(left):
            raise RuntimeError("Unexpected comparison image size")
        changed = 0
        coordinates = []
        for index in range(0, len(left), 4):
            if left[index:index+4] != right[index:index+4]:
                changed += 1
                if len(coordinates) < 5:
                    coordinates.append([index // 4 % 1280, index // 4 // 1280])
        return dict(changed_pixels=changed, total_pixels=1280 * 720,
                    first_changed_coordinates=coordinates,
                    max_channel_delta=max(abs(a-b) for a, b in zip(left, right)))

    def invoke(name, extra, frames=1, camera="detail", count=131072, capture=True):
        destination = output / name
        destination.mkdir(exist_ok=True)
        command = [str(exe), "--preset", "final", "--camera", camera, "--blades", str(count),
                   "--width", "1280", "--height", "720", "--seed", "5650", "--wind-strength", "0",
                   "--fixed-dt", str(1/30), "--warmup", "120", "--frames", str(frames),
                   "--hidden", "--verify", "--validation", "on", "--benchmark", str(destination / "stats.csv")]
        if capture:
            command += ["--capture-dir", str(destination)]
        command += extra
        result = subprocess.run(command, capture_output=True, text=True, timeout=240)
        (destination / "renderer.log").write_text(result.stdout + result.stderr)
        if result.returncode or "VUID-" in result.stderr:
            raise RuntimeError(f"Visual case {name} failed: {result.stderr[-2000:]}")
        rows = list(csv.DictReader((destination / "stats.csv").open()))
        if len(rows) != frames:
            raise RuntimeError(f"Wrong frame count for {name}")
        item = dict(name=name, command=command, returncode=result.returncode, first_frame=rows[0])
        if capture:
            target = images / (name + ".png")
            shutil.copyfile(destination / "frame_000000.png", target)
            item.update(image=target.name, png_sha256=hashlib.sha256(target.read_bytes()).hexdigest())
        report["cases"].append(item)
        print(f"Verified {name}", flush=True)
        return destination

    for feature in [None, "root-color", "rounded-normals", "transmission", "specular"]:
        name = "detail" if feature is None else "detail-no-" + feature
        invoke(name, [] if feature is None else ["--render-disable", feature])
    for feature in [None, "sky", "fog", "shadows"]:
        name = "beauty" if feature is None else "beauty-no-" + feature
        invoke(name, [] if feature is None else ["--render-disable", feature], camera="beauty", count=524288)
    invoke("beauty-basic", ["--shading", "basic"], camera="beauty", count=524288)
    by_name = {case["name"]: case for case in report["cases"]}
    report["feature_comparisons"] = []
    for case in report["cases"]:
        if "-no-" not in case["name"] and case["name"] != "beauty-basic":
            continue
        reference_name = case["name"].split("-", 1)[0]
        reference = by_name[reference_name]
        for field in ["blade_count", "visible"]:
            if case["first_frame"][field] != reference["first_frame"][field]:
                raise RuntimeError(f"Feature ablation changed {field}: {case['name']}")
        delta = difference(reference_name, case["name"])
        if delta["changed_pixels"] < 32:
            raise RuntimeError(f"Feature has no measurable image effect: {case['name']}: {delta}")
        report["feature_comparisons"].append(dict(reference=reference_name, ablation=case["name"], **delta))
    for enabled in ["off", "on"]:
        invoke("occlusion-" + enabled, ["--occlusion", enabled], camera="occluder", count=524288)
        invoke("lod-" + enabled, ["--lod", enabled, "--occlusion", "off", "--preset", "wireframe"],
               camera="overview", count=32768)
    raw = subprocess.check_output(["ffmpeg", "-hide_banner", "-loglevel", "info", "-i", str(images / "occlusion-on.png"),
        "-i", str(images / "occlusion-off.png"), "-lavfi", "psnr", "-f", "null", "-"], stderr=subprocess.STDOUT, text=True)
    report["occlusion_pixel_identical"] = "average:inf" in raw
    report["occlusion_difference"] = difference("occlusion-off", "occlusion-on")
    if not report["occlusion_pixel_identical"]:
        # Atomic compaction may change the winner of quantized depth ties.
        # Measure same-setting repeats before accepting any image difference.
        for enabled in ["off", "on"]:
            invoke("occlusion-" + enabled + "-repeat", ["--occlusion", enabled],
                   camera="occluder", count=524288)
        controls = {enabled: difference("occlusion-" + enabled, "occlusion-" + enabled + "-repeat")
                    for enabled in ["off", "on"]}
        report["occlusion_repeat_controls"] = controls
        repeat_noise = sum(control["changed_pixels"] for control in controls.values())
        changed = report["occlusion_difference"]["changed_pixels"]
        noise_positions = {tuple(point) for control in controls.values()
                           for point in control["first_changed_coordinates"]}
        changed_positions = {tuple(point) for point in report["occlusion_difference"]["first_changed_coordinates"]}
        noise_delta = sum(control["max_channel_delta"] for control in controls.values())
        if (changed > repeat_noise or changed > 4 or not changed_positions <= noise_positions
                or report["occlusion_difference"]["max_channel_delta"] > noise_delta):
            raise RuntimeError(f"Occlusion difference exceeds same-setting repeat noise: {changed}, {controls}")
    report["occlusion_matches_within_repeat_noise"] = True
    if not args.skip_probe:
        probes = {}
        for enabled in ["off", "on"]:
            probe = output / ("interaction-" + enabled + ".csv")
            invoke("interaction-" + enabled,
                   ["--sphere-motion", "sweep", "--interaction", enabled, "--physics-probe", str(probe)],
                   frames=240, camera="interaction", capture=False)
            probes[enabled] = {(int(row["frame"]), int(row["index"])): row
                               for row in csv.DictReader(probe.open())}
        if not probes["on"] or probes["on"].keys() != probes["off"].keys():
            raise RuntimeError("Empty or unmatched interaction cohort")
        trajectories = {}
        for key, row in probes["on"].items():
            reference = probes["off"][key]
            displacement = math.sqrt(sum((float(row["tip_" + axis]) - float(reference["tip_" + axis])) ** 2
                                         for axis in "xyz")) / float(row["rest_height"])
            trajectories.setdefault(key[1], []).append((key[0], displacement, row))
        peaks, recovered = [], []
        for entries in trajectories.values():
            entries.sort()
            peak = max(item[1] for item in entries)
            peaks.append(peak)
            # The sweep finishes at capture time6s; the following2s are recovery.
            recovered.append(entries[-1][1] <= .1 * peak + 1e-5)
        metric = dict(cohort_blades=len(peaks), median_peak_height_ratio=statistics.median(peaks),
                      recovered_fraction=sum(recovered) / len(recovered), recovery_seconds=2,
                      comparison="same zero-wind trajectory with sphere interaction disabled")
        report["interaction"] = metric
        if metric["median_peak_height_ratio"] < .15 or metric["recovered_fraction"] < .9:
            (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
            raise RuntimeError(f"Interaction displacement/recovery failed: {metric}")
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report.get("interaction", {})))


if __name__ == "__main__":
    main()
