Vulkan Grass Rendering
==================================

**University of Pennsylvania, CIS 565: GPU Programming and Architecture, Project 5**

* Nathan Chortek
  * [LinkedIn](https://www.linkedin.com/in/nathan-chortek/), [personal website](https://www.nathanchortek.com/)
* Tested on: Windows 11, AMD Ryzen AI 9 HX 370, NVIDIA GeForce RTX 5090 Laptop GPU


https://github.com/user-attachments/assets/b8591da3-b4d9-4e22-a18c-ea6e62ac6f03


## Features Implemented

In this project, we set up a Vulkan rendering pipeline that simulates a field of grass. Each frame a compute shader runs that simulates the forces acting on each grass blade
and determines which blades should be rendered. Tessellation shaders are used to construct geometry for the blades, and indirect drawing is used for rendering.

### Representing Grass as Bezier Curves

Each blade is represented as a Bezier curve with three control points: `v0`, `v1`, and `v2`. Combined with an `up` vector and four floats representing orientation, height,
width, and stiffness, we have everything needed for tessellation and force simulation.

![](img/blade_model.jpg)

### Simulating Forces

**Gravity**

Given a gravity direction, `D.xyz`, and the magnitude of acceleration, `D.w`, we compute the environmental gravity in our scene as `gE = normalize(D.xyz) * D.w`. We then 
determine the contribution of the gravity with respect to the front facing direction of the blade, `f`, as a term called the "front gravity". Front gravity is computed as 
`gF = (1/4) * ||gE|| * f`. We can then determine the total gravity on the grass blade as `g = gE + gF`.

![](img/gravity.png)

**Recovery**

Recovery corresponds to the counter-force that brings our grass blade back into equilibrium. This is derived in the paper using Hooke's law. In order to determine the recovery 
force, we need to compare the current position of `v2` to its original position before simulation started, `iv2`. At the beginning of our simulation, `v1` and `v2` are 
initialized to be a distance of the blade height along the `up` vector. Once we have `iv2`, we can compute the recovery forces as `r = (iv2 - v2) * stiffness`.

![](img/recovery.png)

**Wind**

Animated Perlin noise is used to form a wind vector field, taking a blade's XZ position as input. Once we have a wind direction, we determine a wind alignment term based on how
perpendicular the blade is to the wind direction, and total wind force (`w`) is calculated as `windDirection * windAlignment`.


https://github.com/user-attachments/assets/73934da2-f6f8-481d-ad14-13c64d6ee679


### Culling

**Orientation**

Blades that are parallel to the wind direction are barely visible, so we cull blades for which the dot product of the view direction and the blade's front face direction is less
than `0.9`.


https://github.com/user-attachments/assets/b8ccdd16-3a08-473c-9bb7-fc01750f25df


**View-frustum**

We also cull blades that are outside of the view-frustum, considering they won't show up in the frame anyway. To determine if a grass blade is in the view-frustum, we compare the
visibility of three points: `v0, v2, and m`, where `m = (1/4)v0 * (1/2)v1 * (1/4)v2`. If all three points are outside of the view-frustum, we cull the grass blade.


https://github.com/user-attachments/assets/70da9e6a-0fbb-448f-80c3-d648fadfa529


**Distance**

The further a blade is from the camera, the less visible it will be to the user. With this in mind, we place blades into buckets based on their distance from the camera, and apply
stochastic culling at increasing rates proportional to distance from the camera.


https://github.com/user-attachments/assets/ed543921-536b-4715-aa70-f219b727733f


## Performance Analysis

![](img/perfMS.png)

| Blade Count | Culling Enabled (ms) | Culling Disabled (ms) | Speedup |
|------------:|---------------------:|----------------------:|--------:|
| 2048        | 0.130                | 0.156                 | 1.21×   |
| 4096        | 0.163                | 0.208                 | 1.27×   |
| 8192        | 0.204                | 0.298                 | 1.46×   |
| 16384       | 0.271                | 0.473                 | 1.75×   |
| 32768       | 0.451                | 0.878                 | 1.95×   |


![](img/perfFPS.png)

| Blade Count | Culling Enabled (FPS) | Culling Disabled (FPS) |
|------------:|----------------:|-----------------:|
| 2048        | 7716.6          | 6393.6           |
| 4096        | 6126.8          | 4813.7           |
| 8192        | 4903.6          | 3354.5           |
| 16384       | 3689.7          | 2112.4           |
| 32768       | 2218.9          | 1138.9           |

Note: All performance measurements are taken as an average over 200 frames in a Release build, with the camera position at (0, 10, 20) looking at (0, 1, 0). The workgroup size for the 
compute pass is fixed at 32.

### Impact of Blade Count

Average frame time increases roughly linearly as blade count increases. The likely explanation here is GPU underutilization: at relatively low blade counts, the GPU has a relatively light compute
workload, leaving it largely unsaturated. As blade count grows, the extra blades add relatively little performance cost on the compute side. After culling, each surviving blade generates a fixed 
amount of geometry and draw work (tessellation plus rasterization of a fixed number of triangles per-blade), explaining the linear trend.

### Impact of Culling

Each frame has a fixed per-frame workload (compute dispatch, presentation, etc) as well as a per-blade workload. At lower blade counts, the fixed portion represents a relatively larger fraction of the
average frame time. As blade count increases, the fixed portion represents a smaller and smaller portion of the frame time. Given that the camera was fixed during performance testing, culling removes
a constant fraction of the per-blade workload. The more per-blade work we have, the larger the speedup from culling will be, explaining why we see a 1.21× speedup at 2048 blades but a 1.95× speedup
at 32768 blades.
