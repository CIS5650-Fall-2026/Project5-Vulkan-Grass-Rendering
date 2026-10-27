#pragma once

namespace ExperimentConfig {
    constexpr bool ENABLED = true;
    constexpr unsigned int BLADE_COUNT = 1u << 17;
    constexpr int WINDOW_WIDTH = 2560;
    constexpr int WINDOW_HEIGHT = 1440;
    constexpr float CAMERA_DISTANCE = 10.0f;
    constexpr unsigned int RANDOM_SEED = 5650u;
    constexpr float FIXED_DELTA_TIME = 1.0f / 60.0f;
    constexpr float SUN_TIME = 9.0f;
}
