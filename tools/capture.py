#!/usr/bin/env python3
"""Capture fixed-step feature demonstrations and encode small palette-based GIFs."""
import argparse
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
    for preset in args.presets:
        directory = (args.output / "capture-frames" / preset).resolve()
        directory.mkdir(parents=True, exist_ok=True)
        camera = "occluder" if preset == "occlusion" else "grazing"
        command = [str(exe), "--preset", preset, "--camera", camera, "--blades", str(args.blades),
                   "--seed", "5650", "--width", "960", "--height", "540", "--fixed-dt", str(1 / 30),
                   "--warmup", "120", "--frames", str(args.frames), "--hidden", "--capture-dir", str(directory)]
        subprocess.run(command, cwd=exe.parent, check=True)
        frames = sorted(directory.glob("*.png"))
        if len(frames) != args.frames:
            raise SystemExit(f"Expected {args.frames} frames for {preset}, found {len(frames)}")
        shutil.copyfile(frames[len(frames) // 2], args.output / (preset + ".png"))
        # A concat list avoids assuming the renderer's frame-number padding.
        listing = directory / "frames.txt"
        listing.write_text("".join("file '" + p.name.replace("'", "'\\''") + "'\nduration 0.0333333333\n" for p in frames))
        subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-f", "concat", "-safe", "0",
                        "-i", str(listing), "-filter_complex",
                        "fps=30,scale=720:-1:flags=lanczos,split[a][b];[a]palettegen=max_colors=128[p];[b][p]paletteuse=dither=sierra2_4a",
                        "-loop", "0", str((args.output / (preset + ".gif")).resolve())], check=True)
        print(f"Captured {preset}", flush=True)


if __name__ == "__main__":
    main()
