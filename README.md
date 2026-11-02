Vulkan Grass Rendering
==================================

**University of Pennsylvania, CIS 565: GPU Programming and Architecture, Project 5**

* Anna Dai [LinkedIn](https://www.linkedin.com/in/nanru-dai-8b33a9261) [Portfolio](https://annaaaddddd.github.io/)
* Tested on: Windows 11, AMD Ryzen 9 5950X @ 3.40GHz 128GB, RTX 3090 Ti (Personal Desktop)

![](img/readme/hero.png)

*The finished grass field.*

## Overview

This project simulates and renders a field of grass in real time with Vulkan, following the method in the [2017 paper by Jahrmann and Wimmer](https://www.cg.tuwien.ac.at/research/publications/2017/JAHRMANN-2017-RRTG/JAHRMANN-2017-RRTG-draft.pdf). Each blade is a quadratic Bezier curve that is updated on the GPU every frame.

![](img/readme/grass_w_wind_movingcam.gif)

*Grass in the wind with a moving camera.*

A frame has two main stages:

* **Compute:** applies forces to every blade, tests each one for culling, and writes the survivors to a second buffer.
* **Graphics:** draws the surviving blades with an indirect draw call, then tessellates each one into a curved triangle.

## Features

### Tessellated Grass Blades

Each blade is stored as four vectors. The first three are the Bezier control points, and the spare `w` components hold the values that describe the blade:

* **`v0`:** the root position, plus the orientation angle.
* **`v1`:** the middle control point, plus the height.
* **`v2`:** the tip control point, plus the width.
* **`up`:** the up vector, plus the stiffness.

Each blade enters the pipeline as a patch with one control point. The tessellation control shader sets all levels to 8, and the evaluation shader uses De Casteljau's algorithm to find the curve position and tangent. It then tapers the width to a point at the tip.

The fragment shader blends from dark green at the root to light green at the tip. Lighting is Lambert with a small ambient term, and it uses the absolute value of the dot product so that both faces are lit.

![](img/readme/static_grass.gif)

*Tessellated blades with no forces applied.*

### Physics Simulation

The compute shader moves `v2` every frame, using the sum of three forces:

* **Gravity:** pulls the tip down, with an extra forward component that makes the blade bend in the direction it faces.
* **Recovery:** pulls the tip back toward its rest position, scaled by the stiffness.
* **Wind:** blows in a fixed direction, with a strength made from two sine waves that vary over time and position. The force is stronger on blades that stand upright, and weaker on blades already leaning along the wind.

After the forces are applied, a validation step keeps the blade plausible. It keeps `v2` above the ground, recomputes `v1` from the new tip position, and corrects both points so that the blade length stays close to its height.

![](img/readme/with_wind.gif)

*Gravity, recovery, and wind applied.*

### Culling

The compute shader appends each surviving blade to the culled blades buffer, using `atomicAdd` on the `vertexCount` field of the indirect draw buffer. `vkCmdDrawIndirect` then reads that count, so the CPU never needs to know how many blades survived. The count is reset with `vkCmdFillBuffer` before each dispatch.

A blade is removed if it fails any of these tests:

* **Orientation:** removes blades seen close to side on, which cover fewer pixels. The threshold is 0.9.
* **View frustum:** removes blades whose `v0`, midpoint, and `v2` are all outside the frustum, with a tolerance of 0.1.
* **Distance:** removes a growing share of blades as their distance from the camera along the ground grows, using 10 buckets and a max distance of 30.

![](img/readme/cull_default.png)

*Default view at 8192 blades. All three tests together draw 29% of the blades.*

![](img/readme/cull_high.png)

*High view at 8192 blades. Distance culling does most of the work here, and the ground shows through at the far end of the field.*

![](img/readme/distance_cull.gif)

*The field thins out as the camera moves away.*

## Performance Analysis

All tests ran in Release at 640 × 480 with 8 × 8 tessellation per blade. Runs were shuffled so that slow drift over the session would not favor one configuration.

* **Repeats:** each configuration ran 3 times for 12 seconds.
* **Warm up:** the first second of each run was dropped.
* **Frame time:** the median of the one second averages in each run, then the mean of those medians across runs.
* **Blades drawn:** read back from the indirect draw buffer, so these counts come from the GPU.

### Number of Blades

![](img/readme/perf_blades.png)

*Frame time against blade count from the default view, with culling off and on.*

| Blades | No culling (ms) | All culling (ms) | Speedup |
| --- | --- | --- | --- |
| 4096 | 0.167 | 0.100 | 1.7× |
| 16384 | 0.560 | 0.213 | 2.6× |
| 65536 | 2.398 | 0.723 | 3.3× |
| 262144 | 9.352 | 2.814 | 3.3× |
| 1048576 | 41.922 | 10.569 | 4.0× |

*Mean frame time for each blade count from the default view.*

Frame time grows close to linearly with blade count from 16K blades up. From 16K to 1M, the blade count grows 64 times, while frame time grows 75 times with no culling and 50 times with all culling.

All culling together makes frames 2.6 to 4.0 times faster in that range. At 1M blades, it raises the frame rate from 24 to 95 FPS. The 4K results are too close to the noise floor to compare reliably: with all culling, the runs vary by 8% between repeats, against about 1% or less everywhere else.

### Culling Tests

![](img/readme/perf_culling.png)

*Frame time for each culling configuration at 65536 blades from the default view.*

| Configuration | Blades drawn | Frame time (ms) | Blades removed | Time saved |
| --- | --- | --- | --- | --- |
| No culling | 100% | 2.398 | 0% | 0% |
| Frustum only | 74% | 1.953 | 26% | 19% |
| Orientation only | 72% | 1.612 | 28% | 33% |
| Distance only | 58% | 1.283 | 42% | 47% |
| All three | 29% | 0.723 | 71% | 70% |

*Blades removed against time saved, compared with no culling, at 65536 blades from the default view.*

All three tests together draw 29% of the blades and save 70% of the frame time. The tests are close to statistically independent: multiplying their individual drawn fractions predicts 31%.

Time saved does not exactly match the share of blades removed:

* **Frustum** saves less than it removes, likely because offscreen blades never reach the fragment stage.
* **Orientation and distance** save about 5 percentage points more than they remove, and the cause is unconfirmed.

### Camera Views

![](img/readme/perf_views.png)

*The same five configurations at 65536 blades from all three camera views.*

| View | Frustum drawn | Orientation drawn | Distance drawn | All drawn | All time saved |
| --- | --- | --- | --- | --- | --- |
| Default (r = 10) | 74% | 72% | 58% | 29% | 70% |
| High (tilted 40° down, r = 16) | 93% | 100% | 53% | 48% | 57% |
| Close (r = 3) | 31% | 75% | 74% | 16% | 71% |

*Share of blades drawn by each test, and the time saved by all three, at 65536 blades.*

The value of each test depends on the camera, so a single view would give a misleading ranking:

* **Frustum** is the strongest test up close, where most blades are behind or beside the camera.
* **Orientation** does nothing from the high view, because the downward view direction keeps the dot product below 0.9.
* **Distance** is the strongest test from the high view, and it gets weaker as the camera moves closer to the field.

The full set of tests saves a similar amount from the default and close views, even though a different test leads in each one.

### Limitations

* Results apply to this scene, window size, and these three cameras.
* All runs use the same blade layout, so the error bars show timing repeatability only.
* Orientation and distance culling change the image, so their savings are not free.

## CMake Changes

* `external/GLFW/CMakeLists.txt`: removed `cmake_policy(SET CMP0042 OLD)`. CMake 4 no longer allows this policy to be set to OLD, and it only affects macOS.
* `src/CMakeLists.txt`: added the `DATA_DIR` compile definition so `main.cpp` can write the performance CSVs to `data/`.

With CMake 4, also set `CMAKE_POLICY_VERSION_MINIMUM` to 3.5 when configuring, because the project declares a minimum version older than CMake 4 accepts.
