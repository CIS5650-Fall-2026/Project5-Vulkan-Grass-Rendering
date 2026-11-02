#!/usr/bin/env bash
# runs the perf matrix: edits the cull defines + NUM_BLADES, rebuilds release, runs each job for a few seconds.
# each run writes data/<label>_<num_blades>_<view>_r<run>.csv (see main.cpp).
# jobs are shuffled so thermal / background drift doesn't line up with one config.
#
# usage (git bash or "bash ./scripts/run_perf.sh" from powershell, repo root):
#   ./scripts/run_perf.sh [seconds_per_run] [runs_per_config]
# don't touch the mouse while it runs, the camera is set by the view argument.

set -e
cd "$(dirname "$0")/.."

SECS=${1:-12}
RUNS=${2:-3}
SHADER=src/shaders/compute.comp
BLADES=src/Blades.h
EXE=../../bin/Release/vulkan_grass_rendering.exe

# full path on purpose: a plain "timeout" can resolve to windows' TIMEOUT.EXE when bash is started from powershell
TIMEOUT=/usr/bin/timeout

# label  blades_exp  orient  frustum  dist      (blades = 1 << exp), all at the default view
CONFIGS=(
  "all_cull     12 1 1 1"
  "no_cull      12 0 0 0"
  "all_cull     14 1 1 1"
  "no_cull      14 0 0 0"
  "all_cull     16 1 1 1"
  "no_cull      16 0 0 0"
  "orient_only  16 1 0 0"
  "frustum_only 16 0 1 0"
  "dist_only    16 0 0 1"
  "all_cull     18 1 1 1"
  "no_cull      18 0 0 0"
  "all_cull     20 1 1 1"
  "no_cull      20 0 0 0"
)

# the five 64k configs again from two other camera views (see main.cpp for what high/close mean)
VIEW_CONFIGS=(
  "all_cull     16 1 1 1"
  "no_cull      16 0 0 0"
  "orient_only  16 1 0 0"
  "frustum_only 16 0 1 0"
  "dist_only    16 0 0 1"
)
VIEWS=(high close)

set_config() {  # exp orient frustum dist
  sed -i "s/^\(constexpr static unsigned int NUM_BLADES = \)[^;]*;/\11 << $1;/" "$BLADES"
  sed -i "s/^\(#define CULL_ORIENTATION \)[01]/\1$2/" "$SHADER"
  sed -i "s/^\(#define CULL_FRUSTUM \)[01]/\1$3/"     "$SHADER"
  sed -i "s/^\(#define CULL_DISTANCE \)[01]/\1$4/"    "$SHADER"
}

build() {
  if ! cmake --build build --config Release --target vulkan_grass_rendering > build/perf_build.log 2>&1; then
    echo "build failed, see build/perf_build.log"
    exit 1
  fi
}

# put the defaults back (and rebuild them) when the script exits, even on ctrl-c
restore() {
  echo "restoring defaults (1 << 13, all culling on) and rebuilding"
  set_config 13 1 1 1
  build
}
trap restore EXIT

# expand every config x view x run into one job line, then shuffle
jobs=()
for cfg in "${CONFIGS[@]}"; do
  for run in $(seq 1 "$RUNS"); do
    jobs+=("$cfg default $run")
  done
done
for cfg in "${VIEW_CONFIGS[@]}"; do
  for view in "${VIEWS[@]}"; do
    for run in $(seq 1 "$RUNS"); do
      jobs+=("$cfg $view $run")
    done
  done
done
mapfile -t jobs < <(printf '%s\n' "${jobs[@]}" | shuf)

for job in "${jobs[@]}"; do
  read -r label exp orient frustum dist view run <<< "$job"
  echo "== $label  blades=$((1 << exp))  orient=$orient frustum=$frustum dist=$dist  view=$view run=$run"

  set_config "$exp" "$orient" "$frustum" "$dist"
  build

  # shaders/images are loaded relative to build/src. timeout kills the window after SECS seconds (exit 124)
  (cd build/src && "$TIMEOUT" "$SECS" "$EXE" "$label" "$view" "$run") || true
done

echo "done, csvs are in data/"
