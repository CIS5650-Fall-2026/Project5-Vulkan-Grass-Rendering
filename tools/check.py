#!/usr/bin/env python3
"""Exercise CLI validation and deterministic Vulkan correctness smoke cases."""
import argparse
import csv
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    exe = args.executable.resolve()
    output = (args.output or Path(tempfile.mkdtemp(prefix="grass-check-"))).resolve()
    output.mkdir(parents=True, exist_ok=True)
    for options, expected in [(["--help"], 0), (["--width", "0"], 1),
                              (["--blades", "-1"], 1), (["--fixed-dt", "nan"], 1), (["--fixed-dt", "0.1"], 1),
                              (["--culling", "invalid"], 1), (["--preset", "invalid"], 1)]:
        result = subprocess.run([str(exe), *options], cwd=exe.parent, text=True, capture_output=True, timeout=15)
        if (result.returncode == 0) != (expected == 0):
            raise SystemExit(f"Unexpected CLI result for {options}: {result.returncode}\n{result.stdout}{result.stderr}")
    cases = [(count, "none", "off", "basic", 50) for count in [0, 1, 127, 128, 129, 8192]]
    cases += [(8192, "all", "off", "forces", 50), (8192, "all", "on", "final", 50),
              (8192, "all", "on", "interaction", 50), (8192, "none", "off", "wireframe", 50),
              (8192, "distance", "off", "forces", 0.01)]
    for index, (count, culling, occlusion, preset, max_distance) in enumerate(cases):
        path = output / f"case-{index}.csv"
        command = [str(exe), "--hidden", "--validation", "on", "--verify", "--blades", str(count),
                   "--preset", preset, "--culling", culling, "--occlusion", occlusion,
                   "--max-distance", str(max_distance), "--warmup", "4", "--frames", "12", "--benchmark", str(path)]
        result = subprocess.run(command, cwd=exe.parent, text=True, capture_output=True, timeout=120)
        (output / f"case-{index}.log").write_text(result.stdout + result.stderr)
        if result.returncode:
            raise SystemExit(f"GPU case {index} failed: see {output / f'case-{index}.log'}")
        with path.open() as stream:
            rows = list(csv.DictReader(line for line in stream if not line.startswith("#")))
        if len(rows) != 12:
            raise SystemExit(f"GPU case {index} wrote {len(rows)} measured rows; expected 12")
        for row in rows:
            visible = int(float(row["visible"]))
            if not 0 <= visible <= count:
                raise SystemExit(f"GPU case {index} has an invalid visible count: {visible}/{count}")
            if culling == "none" and occlusion == "off" and visible != count:
                raise SystemExit(f"GPU case {index} lost blades with culling disabled: {visible}/{count}")
            if max_distance < 1 and visible != 0:
                raise SystemExit(f"GPU case {index} should reject every blade, found {visible}")
        print(f"Passed {count} blades, {preset}, culling={culling}, occlusion={occlusion}", flush=True)
    print(f"CLI and {len(cases)} validation-enabled GPU cases passed. Logs: {output}")


if __name__ == "__main__":
    main()
