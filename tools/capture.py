#!/usr/bin/env python3
"""Capture fixed-step feature demonstrations and encode small palette-based GIFs."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("--output", type=Path, default=Path("img"))
    parser.add_argument("--frames", type=int, default=120)
    parser.add_argument("--blades", type=int, default=524288)
    parser.add_argument("--presets", nargs="+", default=["basic", "forces", "culling", "wireframe", "occlusion", "interaction", "final"])
    args = parser.parse_args()
    exe = args.executable.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    manifest = {"capture_resolution": [1280, 720], "simulation_fps": 30,
                "gif_resolution": [720, 405], "gif_fps": 20, "items": []}
    manifest["source_commit"] = subprocess.check_output(
        ["git", "rev-parse", "HEAD"], cwd=Path(__file__).resolve().parents[1], text=True).strip()
    for preset in args.presets:
        directory = (args.output / "capture-frames" / preset).resolve()
        directory.mkdir(parents=True, exist_ok=True)
        for old in directory.glob("*.png"):
            old.unlink()
        camera = {"occlusion": "occluder", "interaction": "interaction", "final": "beauty"}.get(preset, "grazing")
        frame_count = max(args.frames, 240) if preset == "interaction" else args.frames
        blade_count = 32768 if preset == "wireframe" else args.blades
        command = [str(exe), "--preset", preset, "--camera", camera, "--blades", str(blade_count),
                   "--seed", "5650", "--width", "1280", "--height", "720", "--fixed-dt", str(1 / 30),
                   "--warmup", "120", "--frames", str(frame_count), "--hidden", "--capture-dir", str(directory)]
        if preset == "interaction":
            command += ["--sphere-motion", "sweep", "--wind-strength", "0", "--shading", "enhanced"]
        subprocess.run(command, cwd=exe.parent, check=True)
        frames = sorted(directory.glob("*.png"))
        if len(frames) != frame_count:
            raise SystemExit(f"Expected {frame_count} frames for {preset}, found {len(frames)}")
        shutil.copyfile(frames[len(frames) // 2], args.output / (preset + ".png"))
        # A concat list avoids assuming the renderer's frame-number padding.
        listing = directory / "frames.txt"
        listing.write_text("".join("file '" + p.name.replace("'", "'\\''") + "'\nduration 0.0333333333\n" for p in frames))
        encoding = ["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-f", "concat", "-safe", "0",
                        "-i", str(listing), "-filter_complex",
                        "fps=20,scale=720:-1:flags=lanczos,split[a][b];[a]palettegen=max_colors=128[p];[b][p]paletteuse=dither=bayer:bayer_scale=3",
                        "-loop", "0", str((args.output / (preset + ".gif")).resolve())]
        subprocess.run(encoding, check=True)
        gif = args.output / (preset + ".gif")
        manifest["items"].append(dict(preset=preset, camera=camera, blade_count=blade_count,
                                      frames=frame_count, command=command, encoding_command=encoding,
                                      gif=gif.name, gif_bytes=gif.stat().st_size,
                                      gif_sha256=hashlib.sha256(gif.read_bytes()).hexdigest(),
                                      png=preset + ".png"))
        (args.output / "media.json").write_text(json.dumps(manifest, indent=2) + "\n")
        print(f"Captured {preset}", flush=True)


if __name__ == "__main__":
    main()
