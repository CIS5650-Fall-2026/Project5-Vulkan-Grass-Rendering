# Vulkan Grass Rendering

**University of Pennsylvania, CIS 565: GPU Programming and Architecture, Project 5**

- [Didrik Wiig-Andersen](https://github.com/didrikwiigandersen)
- Tested on: Ubuntu 24.04.4 LTS, AMD EPYC 7R32 (4 vCPUs), 15.4 GiB usable RAM, NVIDIA A10G with 23,028 MiB reported VRAM (AWS g5.xlarge).

![A wind-swept procedural meadow rendered with Vulkan](img/hero.png)

A GPU-driven grass simulator and renderer. Each blade remains a compact quadratic Bézier curve during simulation and visibility testing; the graphics pipeline tessellates surviving curves into tapered geometry. Wind, gravity, elastic recovery, and a moving sphere shape the field without transferring the blade population back to the CPU.

## From curves to a meadow

Clips are captured at 960 × 540 with 30 simulation steps per second, then exported as 640-pixel-wide, 20 FPS GIFs. [Media settings and encoding commands](img/media.json) preserve the capture recipe.

| Stage | Demonstration |
| --- | --- |
| Static curves, tessellation, and basic lighting | ![Static grass](img/basic.gif) |
| Gravity, recovery, and coherent traveling wind | ![Animated forces](img/forces.gif) |
| GPU visibility tests and indirect rendering | ![Culled grass](img/culling.gif) |

The simulation follows [Jahrmann and Wimmer’s responsive grass model](https://www.cg.tuwien.ac.at/research/publications/2017/JAHRMANN-2017-RRTG/JAHRMANN-2017-RRTG-draft.pdf). Four `vec4` values store a blade’s root, curve guide, physical tip, up vector, orientation, rest height, width, and stiffness. Every blade continues simulating, including those outside the camera’s view.

The compute pipeline applies environmental and front gravity, stiffness-based recovery, and a spatially coherent wind field. It projects displaced tips above the ground, reconstructs the guide, and corrects the paper’s approximate curve length. Visible blades are compacted into a device-local vertex buffer and rendered with a GPU-written indirect command. Counter initialization occurs before the visibility dispatch.

Three independently switchable visibility tests remove blades that contribute little to the image:

- **Orientation:** removes nearly edge-on blades using the lateral direction of the blade and a dot-product threshold of 0.98. The narrow threshold preserves coverage within coherently oriented clumps.
- **Frustum:** uses the curve’s root, midpoint, tip, and conservative bounds with Vulkan’s zero-to-one clip-depth convention.
- **Distance:** reduces retained density in stable distance buckets and removes blades beyond the configured range. Selection depends on persistent identity rather than compacted output order.

## Extra Credit

### LOD

The tessellation control shader changes the number of longitudinal segments with camera distance. Geometry detail and retained blade density have separate controls. The wireframe view exposes the geometry change directly. This debug clip uses 32,768 blades so the individual tessellated strips remain visible.

![Distance-dependent tessellation in wireframe](img/wireframe.gif)

| Fixed tessellation | Adaptive tessellation |
| --- | --- |
| ![Fixed tessellation wireframe](img/lod-off.png) | ![Adaptive tessellation wireframe](img/lod-on.png) |

These matching overview captures use 32,768 blades and the same camera, seed, simulation time, and culling. Only longitudinal tessellation changes.

### Occlusion culling

Procedural stones and the interactive sphere provide opaque blockers. A same-frame depth prepass feeds a maximum-depth hierarchy. Visibility tests conservatively compare projected blade bounds against this hierarchy; uncertain bounds and near-plane crossings remain visible. Disabling occlusion also skips its additional prepass and hierarchy work.

![Grass occluded by procedural scene geometry](img/occlusion.gif)

| Occlusion disabled | Occlusion enabled |
| --- | --- |
| ![Opaque blockers without occlusion rejection](img/occlusion-off.png) | ![Opaque blockers with occlusion rejection](img/occlusion-on.png) |

The matching captures are pixel-identical while the visible draw count falls from 86,706 to 59,628. The depth hierarchy removes hidden geometry without changing this image.

### Interactive grass

A controllable sphere pushes the curve’s midpoint and physical tip aside. The same frame snapshot drives collision, object rendering, and visibility. Elastic recovery returns displaced grass toward equilibrium after the sphere passes. Keyboard controls also change wind direction and strength.

![A sphere bends grass as it crosses the field](img/interaction.gif)

### Better rendering

The scene combines a procedural sky, directional sunlight, two-sided grass lighting, root-to-tip color variation, rounded shading normals, restrained transmission and specular highlights, and atmospheric fog. Related blades form coherent clumps, and the ground palette matches the distant field. All geometry and shading are generated in this project; no external art assets are required.

![Final meadow](img/final.gif)

## Performance

All timings below are GPU frame spans in milliseconds, measured in **Release** at **1280 × 720**. Each configuration excludes 120 warmup frames and records 300 frames, repeated three times in a deterministic shuffled order. Reported values are the median of the three run medians. The 87 runs contain **26,100 measured frames**.

| Test environment | Configuration |
| --- | --- |
| GPU | NVIDIA A10G, 23,028 MiB reported VRAM, driver 615.71.10 |
| CPU / memory | AMD EPYC 7R32, 4 vCPUs, 15.4 GiB usable system memory |
| OS / compiler | Ubuntu 24.04.4 LTS, Linux 7.0.0-1012-aws, GCC 13.3.0 |
| SDK / build | Vulkan SDK 1.4.363.0, Vulkan device API 1.4.351, CMake 4.0.3 |
| Fixed scene | Seed 5650, 64 × 64 field, 1/60-second simulation steps |
| Baseline controls | `culling` preset, `overview` camera, 12 fixed length segments, LOD and occlusion off |

The same camera, scene area, wind progression, and simulation stepping are used within each comparison. Every case uses GPU compaction and indirect drawing; the switches control rejection. Validation, screenshots, image encoding, and blade-state verification are disabled during timing. Benchmark submission waits for swapchain acquisition before the opening GPU timestamp.

### Blade-count scaling

| Blades | None | Orientation | Frustum | Distance | All three |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 8,192 | 0.0605 | 0.0609 | 0.0560 | 0.0446 | 0.0439 |
| 32,768 | 0.1487 | 0.1458 | 0.1234 | 0.0732 | 0.0645 |
| 131,072 | 0.5290 | 0.5121 | 0.4221 | 0.2208 | 0.1721 |
| 524,288 | 2.0027 | 1.9398 | 1.5867 | 0.8089 | 0.5997 |
| 1,048,576 | 3.9625 | 3.8348 | 3.1183 | 1.5775 | 1.1603 |

![GPU frame span versus grass population](performance/scaling.svg)

At the larger populations, time grows approximately linearly: doubling from 524,288 to 1,048,576 blades changes the unculled result from 2.0027 to 3.9625 ms, and the combined-culling result from 0.5997 to 1.1603 ms. At one million blades, the three tests together retain about 177,746 blades and provide a **3.42× speedup**, reducing the GPU frame span by **70.7%**.

### What each culling test saves

![Speedup from independently enabled culling tests](performance/culling-speedup.svg)

At 1,048,576 blades, orientation culling gives **1.03×**, frustum culling **1.27×**, and distance culling **2.51×** speedup independently. Their retained populations are approximately 1,010,208, 765,828, and 330,166 blades, respectively. The tests overlap, so their speedups and rejection counts cannot be added.

The orientation threshold of 0.98 removes only the narrowest edge-on blades, preserving dense coverage within coherent clumps. Its modest rejection rate also gives a modest performance benefit. At 8,192 blades it is **0.7% slower** than no culling: 0.0609 versus 0.0605 ms. The absolute difference is small and the run ranges overlap, so this view shows no reliable orientation-culling benefit at that scale.

Distance culling saves the most work in this view by reducing distant density. Physics remains a floor: at one million blades, simulation stays between 0.2924 and 0.2932 ms across the five modes because every blade still simulates. Culling also reduces the number of complete blade records written to the visible buffer: the measured visibility/packing interval falls from 0.2982 to 0.1683 ms with all three tests at one million blades.

### LOD and occlusion

These pairs use the `final` preset, including improved shading and sphere interaction, with the same settings on both sides of each comparison. LOD uses the overview camera; occlusion uses the blocker-heavy occluder camera. They are separate experiments from the baseline table.

| Experiment at 524,288 blades | Disabled | Enabled | Effect |
| --- | ---: | ---: | --- |
| Adaptive tessellation | 0.6223 ms | 0.4908 ms | 21.1% less GPU time, 1.27× speedup; same 89,180 median visible blades |
| Occlusion, with LOD on | 0.5524 ms | 0.5366 ms | 2.9% less GPU time; median survivors fall from about 86,614 to 59,550 |

Occlusion removes **31.2%** of the blades that survive the other tests, but its 0.0543 ms depth-prepass/hierarchy cost and additional visibility work consume most of the drawing savings. Its total benefit is consequently smaller than its survivor reduction. The reported time includes that overhead, and disabling the feature skips it.

### Data and measurement limits

[Raw CSVs, metadata, commands, and source hashes](performance/raw/manifest.json) and the [summary table](performance/summary.csv) preserve the measurements for source revision `687540dd6cdb5afc1a73691b65d9f5619016e98a`. The summary includes minimum and maximum run medians. These results describe this GPU, fixed scene, camera, and density policy; they do not imply the same gains in every view. Runs are short, and GPU clocks and temperature are not recorded.

GPU queries record simulation, visibility, depth work, a graphics tail interval, and the total GPU frame span. The graphics tail can include unfinished opaque fragments and is not an exclusive grass cost. Intervals can overlap and should not be added. Counter readback and presentation are outside the GPU interval. CPU frame time is recorded separately; inverse GPU time is not presented as application FPS.

## Build and run

Use a Vulkan 1.1-capable GPU with tessellation support, the [Vulkan SDK](https://vulkan.lunarg.com/), a current GPU driver, CMake 3.16 or newer, and a C++17 compiler. GLFW, GLM, and STB are already included in the starter.

From the repository root:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/bin/vulkan_grass_rendering
```

On Linux, install the X11 development packages required by bundled GLFW. `tools/setup_gpu_ubuntu.sh` documents the tested Ubuntu GPU-host toolchain setup, including the official NVIDIA driver and LunarG SDK downloads. A hardware-backed display is required even for a hidden GLFW window. On a server without a desktop, reboot after the driver installation, run `sudo bash tools/start_gpu_display.sh`, and set `DISPLAY=:0`. Load the SDK with `source /opt/VulkanSDK/1.4.363.0/setup-env.sh`; verify the selected GPU with `vulkaninfo --summary` and run `vkcube`. The display helper uses a local Unix socket and disables X11 TCP listening.

| Input | Action |
| --- | --- |
| Left / right mouse drag | Orbit / zoom the camera |
| W, A, S, D and Q, E | Move the sphere horizontally and vertically |
| Arrow keys | Rotate wind and change its strength |
| 1, 2, 3, 4 | Toggle orientation, frustum, distance, and occlusion culling |
| L / F / H | Toggle adaptive tessellation / wireframe / controls overlay |
| Space / R / Escape | Pause / reset the grass / exit |

Use `--help` for all command-line options. Examples:

```sh
./build/bin/vulkan_grass_rendering --blades 1048576 --culling all
./build/bin/vulkan_grass_rendering --preset wireframe --camera grazing
./build/bin/vulkan_grass_rendering --preset interaction
python3 tools/check.py build/bin/vulkan_grass_rendering
python3 tools/check_interaction.py build/bin/vulkan_grass_rendering --output performance/checks/interaction
python3 tools/benchmark.py build/bin/vulkan_grass_rendering --extras
python3 tools/analyze.py performance/raw
python3 tools/capture.py build/bin/vulkan_grass_rendering
```

The X11 interaction smoke test additionally uses `xdotool` and FFmpeg as development tools; the Ubuntu setup helper installs both. Debug builds enable Vulkan validation by default. Automated correctness, benchmark, and capture modes finish after the requested frame count and return an error code on failure.

**Build-system changes:** `CMakeLists.txt`, `src/CMakeLists.txt`, and `external/GLFW/CMakeLists.txt` were changed beyond source-file lists. Changes select C++17, compile GLSL to SPIR-V on supported platforms, build the executable from the current renderer/scene entry points, and place generated shaders beside the executable in `build/bin`. The bundled GLFW CMake minimum was raised to 3.16 and its obsolete CMP0042 OLD override removed for modern CMake compatibility. Dependency C/C++ source remains unchanged.

## Verification

[Recorded checks](performance/validation.json) include the SDK cube, all 13 compiled/validated shaders, Debug and Release builds, and 11 validation-enabled GPU cases. Cases cover zero blades, partial workgroups, all survivors, a populated field with zero survivors, physics, interaction, wireframe, and occlusion. The GPU check enforces finite state, ground constraints, counter bounds, and approximate length within 1% plus a small absolute tolerance.

The X11 smoke test exercises resize and keyboard controls, verifies identical paused pixels and changed pixels after resuming, and checks that early closure of an automated run returns failure. A separate native-window-manager check confirms minimize/restore. Matching occlusion captures are pixel-identical, and a relocated binary runs while the original shader directory is hidden.

Validation uses `VK_LAYER_VALIDATE_SYNC=true` with `--validation on`. Linux/NVIDIA is the tested platform; Windows build paths are retained but were not runtime-tested here.

## References and provenance

- [CIS 5650 Project 5 starter and assignment](https://github.com/CIS5650-Fall-2026/Project5-Vulkan-Grass-Rendering), baseline `d3c78889caadbcad7bc3b894b8714e34c62f3361`.
- [Responsive Real-Time Grass Rendering for General 3D Scenes](https://www.cg.tuwien.ac.at/research/publications/2017/JAHRMANN-2017-RRTG/JAHRMANN-2017-RRTG-draft.pdf), Klemens Jahrmann and Michael Wimmer, 2017. Mathematical model and algorithm reference.
- The supplied Introduction to Vulkan decks and Project 5 recitation slides.
- [Vulkan specification](https://registry.khronos.org/vulkan/) and [synchronization guidance](https://docs.vulkan.org/guide/latest/synchronization_examples.html). API reference.
- [Tessellation tutorial](https://ogldev.org/www/tutorial30/tutorial30.html). Conceptual reference; no sample code imported.
- [Procedural Grass in Ghost of Tsushima](https://www.youtube.com/watch?v=Ibe1JBF5i5Y), Eric Wohllaib, GDC 2021. Visual concepts studied through the official presentation slides.
- [README presentation guide](https://github.com/pjcozzi/Articles/blob/master/CIS565/GitHubRepo/README.md).

All project implementation additions are original. Existing GLFW, GLM, and STB files are supplied by the designated starter and retain their respective notices. No code from completed student projects, external tutorials, or earlier course submissions was reused. FFmpeg is used as an external media-encoding tool, not linked into the renderer.
