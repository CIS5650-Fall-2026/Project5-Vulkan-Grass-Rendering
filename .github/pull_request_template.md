<!-- Title: Project 5: Zachary Kelly -->

[Repo Link](https://github.com/zkelly1/Project5-Vulkan-Grass-Rendering)

Implemented grass simulation with gravity, recovery, wind, curve-length correction,
and indirect drawing of tessellated Bezier blades. Includes orientation, frustum,
and distance culling, with performance comparisons across blade counts.

Extra features:

- Distance-based tessellation LOD.
- Interactive wind strength and direction.
- Procedural sky, sunlight, backlighting, and per-blade color variation.
- A movable sphere with soft grass collision and conservative occlusion culling.

Modified `src/CMakeLists.txt` to find the shader compiler, rebuild shader outputs
when their sources change, and set the Visual Studio debugger working directory.
The root `CMakeLists.txt` is unchanged.

Debug and Release builds passed, along with Vulkan synchronization validation,
GPU buffer checks, and image comparisons. The README includes animated demos,
raw benchmark data, and the limitations of the implementation.
