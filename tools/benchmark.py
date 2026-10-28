#!/usr/bin/env python3
"""Run reproducible renderer experiments and preserve every command and raw CSV."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import random
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("--output", type=Path, default=Path("performance/raw"))
    parser.add_argument("--counts", nargs="+", type=int,
                        default=[8192, 32768, 131072, 524288, 1048576])
    parser.add_argument("--repetitions", type=int, default=3)
    parser.add_argument("--warmup", type=int, default=120)
    parser.add_argument("--frames", type=int, default=300)
    parser.add_argument("--camera", choices=["overview", "grazing", "occluder"], default="overview")
    parser.add_argument("--extras", action="store_true", help="Also compare LOD and occlusion at 524288 blades")
    args = parser.parse_args()
    if min(args.counts) < 0 or args.frames < 1 or args.repetitions < 1 or args.warmup < 0:
        parser.error("Counts and warmup must be nonnegative; frames and repetitions must be positive")
    exe = args.executable.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    cases = []
    for count in args.counts:
        for mode in ["none", "orientation", "frustum", "distance", "all"]:
            for rep in range(args.repetitions):
                cases.append(dict(count=count, culling=mode, lod="off", occlusion="off",
                                  camera=args.camera, preset="culling", repetition=rep, experiment="culling"))
    if args.extras:
        for lod, occlusion, camera, experiment in [
            ("off", "off", "overview", "lod"), ("on", "off", "overview", "lod"),
            ("on", "off", "occluder", "occlusion"), ("on", "on", "occluder", "occlusion")]:
            for rep in range(args.repetitions):
                cases.append(dict(count=524288, culling="all", lod=lod, occlusion=occlusion,
                                  camera=camera, preset="final", repetition=rep, experiment=experiment))
    random.Random(5650).shuffle(cases)
    commit = subprocess.run(["git", "rev-parse", "HEAD"], text=True, capture_output=True).stdout.strip()
    source_root = Path(__file__).resolve().parents[1]
    source_paths = [source_root / "CMakeLists.txt", source_root / "external/CMakeLists.txt",
                    source_root / "external/GLFW/CMakeLists.txt"]
    source_paths += [p for p in (source_root / "src").rglob("*")
                     if p.suffix in {".h", ".cpp", ".comp", ".vert", ".frag", ".tesc", ".tese", ".glsl"}
                     or p.name == "CMakeLists.txt"]
    source_paths += [p for p in (source_root / "tools").iterdir() if p.suffix in {".py", ".sh", ".cpp", ".h"}]
    source_hashes = {str(p.relative_to(source_root)): hashlib.sha256(p.read_bytes()).hexdigest()
                     for p in sorted(source_paths)}
    try:
        gpu = subprocess.run(["nvidia-smi", "--query-gpu=name,driver_version,memory.total", "--format=csv,noheader"],
                             text=True, capture_output=True)
        gpu_description = gpu.stdout.strip() or "See renderer metadata"
    except FileNotFoundError:
        gpu_description = "See renderer metadata"
    cpu_description = platform.processor()
    if Path("/proc/cpuinfo").exists():
        cpu_description = next((line.split(":", 1)[1].strip() for line in Path("/proc/cpuinfo").read_text().splitlines()
                                if line.startswith("model name")), cpu_description)
    memory_description = "See renderer metadata"
    if Path("/proc/meminfo").exists():
        memory_description = Path("/proc/meminfo").read_text().splitlines()[0]
    manifest = dict(source_commit=commit, source_sha256=source_hashes,
                    platform=platform.platform(), processor=cpu_description,
                    logical_cpus=os.cpu_count(), memory=memory_description, gpu=gpu_description, width=1280, height=720,
                    seed=5650, field_size=64, fixed_dt=1 / 60, warmup=args.warmup,
                    measured_frames=args.frames, repetitions=args.repetitions, runs=[])
    manifest_path = args.output / "manifest.json"
    for number, case in enumerate(cases, 1):
        label = (f"{case['experiment']}_{case['count']}_{case['culling']}_lod-{case['lod']}_"
                 f"occ-{case['occlusion']}_{case['camera']}_r{case['repetition']}")
        target = (args.output / (label + ".csv")).resolve()
        command = [str(exe), "--width", "1280", "--height", "720", "--seed", "5650",
                   "--field-size", "64", "--max-distance", "50", "--fixed-dt", str(1 / 60),
                   "--blades", str(case["count"]), "--preset", case["preset"], "--camera", case["camera"],
                   "--culling", case["culling"], "--lod", case["lod"], "--occlusion", case["occlusion"],
                   "--validation", "off", "--warmup", str(args.warmup), "--frames", str(args.frames),
                   "--hidden", "--benchmark", str(target)]
        print(f"[{number}/{len(cases)}] {label}", flush=True)
        started = time.time()
        result = subprocess.run(command, cwd=exe.parent, text=True, capture_output=True, timeout=600)
        (args.output / (label + ".log")).write_text(result.stdout + result.stderr)
        manifest["runs"].append(dict(**case, csv=target.name, command=command,
                                     elapsed_seconds=time.time() - started, returncode=result.returncode))
        manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")
        if result.returncode:
            raise SystemExit(f"Failed: {label}; see its log (exit {result.returncode})")
    print(f"Saved {len(cases)} completed runs to {args.output}")


if __name__ == "__main__":
    main()
