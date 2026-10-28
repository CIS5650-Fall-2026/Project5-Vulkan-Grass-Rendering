#include "Scene.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <exception>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

struct SceneGeometryTestAccess {
    static void ForceBoundedPlacementFailure(Scene& scene) {
        Scene::StaticFootprint footprint;
        footprint.hull = {glm::vec2(-100.0f, -100.0f), glm::vec2(100.0f, -100.0f),
                          glm::vec2(100.0f, 100.0f), glm::vec2(-100.0f, 100.0f)};
        footprint.minimum = glm::vec2(-100.0f);
        footprint.maximum = glm::vec2(100.0f);
        scene.staticFootprints_.assign(1u, std::move(footprint));
        scene.options_.bladeCount = 1u;
        scene.GenerateBlades();
    }
};

namespace {

std::array<int, GLFW_KEY_LAST + 1> gKeys{};
std::array<int, GLFW_MOUSE_BUTTON_LAST + 1> gMouseButtons{};
double gCursorX = 0.0;
double gCursorY = 0.0;
int gShouldClose = GLFW_FALSE;

GLFWwindow* TestWindow() {
    return reinterpret_cast<GLFWwindow*>(static_cast<uintptr_t>(1u));
}

void ResetInput() {
    gKeys.fill(GLFW_RELEASE);
    gMouseButtons.fill(GLFW_RELEASE);
    gCursorX = 0.0;
    gCursorY = 0.0;
    gShouldClose = GLFW_FALSE;
}

[[noreturn]] void Fail(const std::string& message) {
    throw std::runtime_error(message);
}

void Check(bool condition, const std::string& message) {
    if (!condition) {
        Fail(message);
    }
}

bool Near(float lhs, float rhs, float tolerance = 1.0e-5f) {
    return std::abs(lhs - rhs) <= tolerance;
}

bool Finite(const glm::vec4& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z) && std::isfinite(value.w);
}

bool Finite(const glm::mat4& matrix) {
    for (int column = 0; column < 4; ++column) {
        if (!Finite(matrix[column])) {
            return false;
        }
    }
    return true;
}

glm::vec3 ExpectedCameraPosition(const glm::vec3& target, float yaw, float pitch,
                                 float distance) {
    const float horizontal = std::cos(pitch) * distance;
    return target + glm::vec3(std::sin(yaw) * horizontal,
                              std::sin(pitch) * distance,
                              std::cos(yaw) * horizontal);
}

void CheckClipPoint(const glm::mat4& matrix, const glm::vec3& point,
                    const std::string& label, float tolerance = 2.0e-3f) {
    const glm::vec4 clip = matrix * glm::vec4(point, 1.0f);
    Check(Finite(clip) && std::abs(clip.w) > 1.0e-7f, label + " produced invalid clip coordinates");
    const glm::vec3 ndc = glm::vec3(clip) / clip.w;
    Check(ndc.x >= -1.0f - tolerance && ndc.x <= 1.0f + tolerance &&
              ndc.y >= -1.0f - tolerance && ndc.y <= 1.0f + tolerance &&
              ndc.z >= -tolerance && ndc.z <= 1.0f + tolerance,
          label + " lies outside the cached light clip volume");
}

RunOptions SmallOptions(uint32_t count = 256u, float fieldSize = 64.0f) {
    RunOptions options;
    options.bladeCount = count;
    options.fieldSize = fieldSize;
    options.camera = "overview";
    return options;
}

float UnitFloat(uint32_t bits) {
    return static_cast<float>(bits >> 8u) * (1.0f / 16'777'216.0f);
}

std::vector<char*> Arguments(std::vector<std::string>& values) {
    std::vector<char*> result;
    result.reserve(values.size());
    for (std::string& value : values) {
        result.push_back(value.data());
    }
    return result;
}

RunOptions Parse(std::initializer_list<const char*> arguments) {
    std::vector<std::string> values;
    values.reserve(arguments.size() + 1u);
    values.emplace_back("test_scene_geometry");
    for (const char* argument : arguments) {
        values.emplace_back(argument);
    }
    std::vector<char*> pointers = Arguments(values);
    return ParseOptions(static_cast<int>(pointers.size()), pointers.data());
}

void ExpectInvalid(std::initializer_list<const char*> arguments) {
    try {
        (void)Parse(arguments);
    } catch (const std::invalid_argument&) {
        return;
    }
    Fail("invalid command line was accepted");
}

void CheckBladeFinite(const Blade& blade) {
    Check(Finite(blade.v0) && Finite(blade.v1) && Finite(blade.v2) && Finite(blade.up),
          "blade contains a non-finite component");
    Check(blade.v1.w >= 0.08f && blade.v1.w <= 1.2f, "blade height is outside its allowed range");
    Check(blade.v2.w > 0.0f && blade.v2.w <= 0.06f, "blade width is outside its allowed range");
    Check(blade.up.w >= 7.0f && blade.up.w <= 13.0f, "blade stiffness changed range");
}

void TestCountsAndDefaultMesh() {
    const std::array<uint32_t, 6> counts{0u, 1u, 31u, 32u, 33u, 8192u};
    for (uint32_t count : counts) {
        Scene scene(SmallOptions(count));
        Check(scene.Blades().size() == count, "scene did not preserve the exact blade count");
        for (const Blade& blade : scene.Blades()) {
            CheckBladeFinite(blade);
        }
    }

    Scene scene(SmallOptions(33u));
    Check(scene.Vertices().size() == 4971u, "default 64-unit mesh vertex count changed");
    Check(scene.Indices().size() == 23136u, "default 64-unit mesh index count changed");
    Check(scene.Draws().size() == 3u, "default draw count changed");
    const std::array<SceneDraw, 3> expected{{{0u, 1536u, 0u, false},
                                             {1536u, 1296u, 1u, false},
                                             {2832u, 20304u, 2u, true}}};
    for (std::size_t i = 0; i < expected.size(); ++i) {
        const SceneDraw& actual = scene.Draws()[i];
        Check(actual.firstIndex == expected[i].firstIndex &&
                  actual.indexCount == expected[i].indexCount &&
                  actual.material == expected[i].material &&
                  actual.movable == expected[i].movable,
              "default draw range changed");
    }
}

void TestDeterminismAndClearance() {
    RunOptions options = SmallOptions(8192u);
    options.seed = 0x71c5a29bu;
    Scene first(options);
    Scene second(options);
    Check(first.Blades().size() == second.Blades().size(), "deterministic scenes differ in size");
    Check(std::memcmp(first.Blades().data(), second.Blades().data(),
                      first.Blades().size() * sizeof(Blade)) == 0,
          "identical options did not produce bit-identical blades");

    std::mt19937 material(options.seed);
    bool sawUnchanged = false;
    bool sawFringe = false;
    std::vector<std::size_t> nearest;
    nearest.reserve(256u);
    for (std::size_t i = 0; i < first.Blades().size(); ++i) {
        const Blade& blade = first.Blades()[i];
        (void)material();
        (void)material();
        const float authoredHeight = 0.6f + 0.6f * UnitFloat(material());
        const float authoredWidth = 0.025f + 0.035f * UnitFloat(material());
        (void)material();

        CheckBladeFinite(blade);
        const glm::vec3 root(blade.v0);
        const float distance = first.StaticObstacleDistance(root);
        const float required = 1.01f * blade.v1.w + 0.5f * blade.v2.w + 0.01f;
        Check(std::isinf(distance) || distance + 2.0e-5f >= required,
              "blade violates the static-obstacle clearance envelope");
        Check(Near(blade.v1.x, root.x, 2.0e-6f) && Near(blade.v1.z, root.z, 2.0e-6f),
              "guide point is not vertical above its root");
        Check(glm::distance(glm::vec3(blade.v2), root) <= 1.01f * blade.v1.w + 2.0e-5f,
              "rest tip exceeds the declared reach envelope");

        const float heightScale = blade.v1.w / authoredHeight;
        const float widthScale = blade.v2.w / authoredWidth;
        Check(Near(heightScale, widthScale, 3.0e-5f),
              "fringe placement did not scale height and width proportionally");
        Check(heightScale <= 1.0f + 2.0e-6f, "placement enlarged an authored blade");
        sawUnchanged = sawUnchanged || heightScale >= 1.0f - 2.0e-6f;
        sawFringe = sawFringe || heightScale < 0.98f;
        if (std::isfinite(distance) && nearest.size() < nearest.capacity()) {
            nearest.push_back(i);
        }
    }
    Check(sawUnchanged, "no far-field blade retained its authored material dimensions");
    Check(sawFringe, "test scene did not exercise shortened obstacle fringe blades");

    constexpr float pi = 3.14159265358979323846f;
    const std::array<float, 5> curveParameters{0.0f, 0.25f, 0.5f, 0.75f, 1.0f};
    for (std::size_t index : nearest) {
        const Blade& blade = first.Blades()[index];
        const glm::vec2 root(blade.v0.x, blade.v0.z);
        for (int directionIndex = 0; directionIndex < 16; ++directionIndex) {
            const float angle = 2.0f * pi * static_cast<float>(directionIndex) / 16.0f;
            const glm::vec2 bend(std::cos(angle), std::sin(angle));
            const glm::vec2 lateral(-bend.y, bend.x);
            for (float parameter : curveParameters) {
                const glm::vec2 center = root + bend * (1.01f * blade.v1.w * parameter);
                for (float side : {-1.0f, 1.0f}) {
                    const glm::vec2 ribbon = center + lateral * (0.5f * blade.v2.w * side);
                    const float sampleDistance = first.StaticObstacleDistance(
                        glm::vec3(ribbon.x, 0.0f, ribbon.y));
                    Check(sampleDistance + 3.0e-4f >= 0.01f,
                          "extreme bent ribbon sample penetrates static geometry");
                }
            }
        }
    }
}

void TestFootprintsAndSphereArea() {
    Scene scene(SmallOptions(16384u));
    const SceneDraw& stone = scene.Draws().at(1u);
    for (uint32_t offset = 0; offset < stone.indexCount; ++offset) {
        const uint32_t vertexIndex = scene.Indices().at(stone.firstIndex + offset);
        const glm::vec3 point = scene.Vertices().at(vertexIndex).position;
        Check(scene.StaticObstacleDistance(point) <= 2.0e-5f,
              "a static mesh vertex is not contained by its per-object footprint");
    }

    std::size_t rootsUnderSphere = 0u;
    for (const Blade& blade : scene.Blades()) {
        if (glm::length(glm::vec2(blade.v0.x, blade.v0.z)) < 0.95f) {
            ++rootsUnderSphere;
        }
    }
    Check(rootsUnderSphere > 0u, "movable sphere area was incorrectly masked out of grass placement");
}

void TestFieldSizesAndMillionTiming() {
    for (float fieldSize : {1.0f, 2.0f, 8.0f, 64.0f, 512.0f}) {
        Scene scene(SmallOptions(1024u, fieldSize));
        Check(scene.Blades().size() == 1024u, "field-size case lost blades");
        for (const Blade& blade : scene.Blades()) {
            const float half = fieldSize * 0.5f;
            Check(blade.v0.x >= -half && blade.v0.x <= half &&
                      blade.v0.z >= -half && blade.v0.z <= half,
                  "replacement root escaped the field");
        }
    }

    const auto begin = std::chrono::steady_clock::now();
    Scene million(SmallOptions(1'000'000u));
    const double milliseconds = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - begin).count();
    Check(million.Blades().size() == 1'000'000u, "million-blade case lost blades");
    std::cout << "CPU scene generation (1,000,000 blades): " << milliseconds << " ms\n";
}

void TestBoundedFailure() {
    Scene scene(SmallOptions(0u, 8.0f));
    try {
        SceneGeometryTestAccess::ForceBoundedPlacementFailure(scene);
    } catch (const std::runtime_error& error) {
        const std::string message(error.what());
        Check(message.find("blade") != std::string::npos &&
                  message.find("field") != std::string::npos,
              "bounded-placement error does not identify the blade and field");
        return;
    }
    Fail("synthetic fully blocked field did not terminate with a placement error");
}

void TestCli() {
    const RunOptions valid = Parse({"--preset", "basic", "--interaction", "on",
                                    "--shading", "enhanced", "--sphere-motion", "sweep",
                                    "--wind-strength", "18", "--render-disable", "sky,fog",
                                    "--render-disable", "shadows", "--sun-azimuth", "-140",
                                    "--sun-elevation", "25", "--physics-probe", "probe.csv",
                                    "--camera", "beauty", "--verify"});
    Check(valid.sphereMotion == "sweep", "sphere motion option was not retained");
    Check(Near(valid.windStrength, 18.0f), "wind strength option was not retained");
    Check(Near(valid.sunAzimuth, 220.0f), "sun azimuth was not normalized to [0,360)");
    Check(Near(valid.sunElevation, 25.0f), "sun elevation option was not retained");
    Check(valid.physicsProbePath == "probe.csv", "physics probe path was not retained");
    Check((valid.renderFeatureMask & (1u | 32u | 64u)) == 0u,
          "repeated render-disable options did not accumulate");

    const RunOptions orderA = Parse({"--preset", "final", "--interaction", "off",
                                     "--shading", "basic", "--camera", "overview"});
    const RunOptions orderB = Parse({"--shading", "basic", "--interaction", "off",
                                     "--camera", "overview", "--preset", "final"});
    Scene sceneA(orderA);
    Scene sceneB(orderB);
    const uint32_t flagsA = sceneA.MakeUniform(640u, 480u, 1.0f / 60.0f, 0.0f).flags.w;
    const uint32_t flagsB = sceneB.MakeUniform(640u, 480u, 1.0f / 60.0f, 0.0f).flags.w;
    Check(flagsA == flagsB && (flagsA & (InteractiveSphere | BetterShading)) == 0u,
          "preset feature overrides are order-dependent");

    Check(Parse({"--preset", "final"}).camera == "beauty", "final preset did not resolve auto camera");
    Check(Parse({"--preset", "interaction"}).camera == "interaction",
          "interaction preset did not resolve auto camera");
    Check(Parse({"--preset", "basic"}).camera == "overview",
          "basic preset did not resolve auto camera");
    Check(Parse({"--physics-probe", "probe.csv", "--verify"}).verify,
          "physics-probe dependency was evaluated before all arguments");

    ExpectInvalid({"--sphere-motion", "orbit"});
    ExpectInvalid({"--interaction", "yes"});
    ExpectInvalid({"--wind-strength", "-0.01"});
    ExpectInvalid({"--wind-strength", "18.01"});
    ExpectInvalid({"--wind-strength", "nan"});
    ExpectInvalid({"--shading", "fancy"});
    ExpectInvalid({"--render-disable", ""});
    ExpectInvalid({"--render-disable", "sky,,fog"});
    ExpectInvalid({"--render-disable", "sky,unknown"});
    ExpectInvalid({"--sun-elevation", "0"});
    ExpectInvalid({"--sun-elevation", "90"});
    ExpectInvalid({"--sun-azimuth", "inf"});
    ExpectInvalid({"--physics-probe", "probe.csv"});
    ExpectInvalid({"--physics-probe", ""});
    ExpectInvalid({"--camera", "close"});
    ExpectInvalid({"--fixed-dt", "0.034"});
}

void TestCamerasAndUniformContract() {
    struct CameraCase {
        const char* name;
        glm::vec3 target;
        float yaw;
        float pitch;
        float distance;
        float verticalFov;
    };
    constexpr float pi = 3.14159265358979323846f;
    constexpr float fieldSize = 64.0f;
    const std::array<CameraCase, 6> cases{{
        {"overview", glm::vec3(0.0f, 0.95f, -fieldSize * 0.055f), 0.72f, 0.21f,
         glm::clamp(fieldSize * 0.58f, 12.0f, 300.0f), 55.0f},
        {"grazing", glm::vec3(0.0f, 0.82f, -fieldSize * 0.08f), 0.20f, 0.065f,
         glm::clamp(fieldSize * 0.47f, 10.0f, 260.0f), 55.0f},
        {"occluder", glm::vec3(fieldSize * 0.035f, 0.82f, -fieldSize * 0.08f), -0.82f,
         0.055f, glm::clamp(fieldSize * 0.47f, 10.0f, 260.0f), 55.0f},
        {"interaction", glm::vec3(0.0f, 0.55f, 0.0f), 0.2f, 0.22f, 6.5f, 55.0f},
        {"detail", glm::vec3(-2.5f, 0.55f, 3.0f), 0.25f, 0.16f, 4.0f, 55.0f},
        {"beauty", glm::vec3(0.0f, 0.95f, -fieldSize * 0.055f), 0.6f, 0.015f,
         glm::clamp(0.5f * fieldSize, 12.0f, 300.0f), 70.0f},
    }};
    for (const CameraCase& camera : cases) {
        RunOptions options = SmallOptions(32u, fieldSize);
        options.camera = camera.name;
        Scene scene(options);
        const FrameUniform uniform = scene.MakeUniform(1600u, 900u, 1.0f / 60.0f, 0.0f);
        const glm::vec3 expected = ExpectedCameraPosition(camera.target, camera.yaw,
                                                          camera.pitch, camera.distance);
        Check(glm::distance(glm::vec3(uniform.cameraTime), expected) <= 2.0e-4f,
              std::string("camera values changed for ") + camera.name);
        const float recoveredFov = 2.0f * std::atan(1.0f / std::abs(uniform.proj[1][1])) *
                                   180.0f / pi;
        Check(Near(recoveredFov, camera.verticalFov, 2.0e-3f),
              std::string("vertical FOV is incorrect for ") + camera.name);
    }

    RunOptions direct = SmallOptions(32u, fieldSize);
    direct.camera = "auto";
    Scene scene(direct);
    const FrameUniform uniform = scene.MakeUniform(1280u, 720u, 1.0f / 60.0f, 0.0f);
    const glm::vec3 expectedSun(std::cos(25.0f * pi / 180.0f) * std::cos(220.0f * pi / 180.0f),
                                std::sin(25.0f * pi / 180.0f),
                                std::cos(25.0f * pi / 180.0f) * std::sin(220.0f * pi / 180.0f));
    Check(glm::distance(glm::vec3(uniform.lighting), expectedSun) <= 2.0e-5f,
          "uniform lighting is not normalized surface-to-sun direction");
    Check(uniform.renderControl.x == RenderFeatureAll, "default render feature mask is not 127");
    Check(Near(uniform.collider.y, 1.0f) && Near(uniform.collider.w, 1.0f),
          "default sphere is not grounded at center y=radius=1");
    Check(Near(glm::length(glm::vec2(uniform.wind.x, uniform.wind.z)), std::sqrt(17.0f), 2.0e-5f),
          "default wind strength is not sqrt(17)");
}

void TestLightVolumeAndStability() {
    RunOptions options = SmallOptions(2048u);
    options.camera = "beauty";
    Scene scene(options);
    const FrameUniform before = scene.MakeUniform(1280u, 720u, 1.0f / 60.0f, 2.0f);
    Check(Finite(before.lightViewProj), "light matrix contains a non-finite value");

    for (const SceneVertex& vertex : scene.Vertices()) {
        CheckClipPoint(before.lightViewProj, vertex.position, "scene vertex", 5.0e-3f);
    }
    const float half = options.fieldSize * 0.5f;
    const float maximumSphereY = std::max(6.0f, options.fieldSize * 0.25f);
    for (float x : {-half - 1.0f, half + 1.0f}) {
        for (float y : {0.0f, maximumSphereY + 1.0f}) {
            for (float z : {-half - 1.0f, half + 1.0f}) {
                CheckClipPoint(before.lightViewProj, glm::vec3(x, y, z),
                               "permitted sphere bound", 5.0e-3f);
            }
        }
    }
    for (const Blade& blade : scene.Blades()) {
        CheckClipPoint(before.lightViewProj, glm::vec3(blade.v0), "grass root", 5.0e-3f);
        CheckClipPoint(before.lightViewProj,
                       glm::vec3(blade.v0) + glm::vec3(0.0f, 1.01f * blade.v1.w, 0.0f),
                       "grass reach", 5.0e-3f);
    }

    ResetInput();
    scene.Update(TestWindow(), 1.0f / 30.0f, 2.0f);
    gMouseButtons[GLFW_MOUSE_BUTTON_LEFT] = GLFW_PRESS;
    gCursorX = 80.0;
    gCursorY = -35.0;
    gKeys[GLFW_KEY_W] = GLFW_PRESS;
    scene.Update(TestWindow(), 1.0f / 30.0f, 2.1f);
    const FrameUniform after = scene.MakeUniform(800u, 800u, 1.0f / 30.0f, 2.1f);
    Check(std::memcmp(&before.lightViewProj, &after.lightViewProj, sizeof(glm::mat4)) == 0,
          "camera or sphere movement changed the cached light matrix");
}

void TestSweepAndInputEdges() {
    RunOptions sweep = SmallOptions(64u);
    sweep.sphereMotion = "sweep";
    sweep.warmup = 10u;
    sweep.fixedDelta = 0.02f;
    sweep.verify = true;
    Scene scene(sweep);
    const auto colliderAt = [&](float time) {
        scene.Update(nullptr, sweep.fixedDelta, time);
        return scene.MakeUniform(640u, 480u, sweep.fixedDelta, time).collider;
    };
    Check(Near(colliderAt(0.0f).x, -4.0f), "automated warmup did not hold sweep baseline");
    Check(Near(colliderAt(0.18f).x, -4.0f), "late warmup did not hold sweep baseline");
    Check(Near(colliderAt(0.20f).x, -4.0f), "first measured frame did not begin at baseline");
    Check(Near(colliderAt(1.20f).x, -4.0f), "one-second measured baseline was not preserved");
    Check(Near(colliderAt(3.70f).x, 0.0f, 2.0e-4f), "five-second sweep midpoint is incorrect");
    Check(Near(colliderAt(6.20f).x, 4.0f), "sweep did not reach its endpoint");
    Check(Near(colliderAt(8.20f).x, 4.0f), "two-second recovery hold is incorrect");

    ResetInput();
    gKeys[GLFW_KEY_E] = GLFW_PRESS;
    scene.Update(TestWindow(), 0.1f, 0.0f);
    const glm::vec4 manuallyMoved = scene.MakeUniform(640u, 480u, 0.1f, 0.0f).collider;
    Check(manuallyMoved.y > 1.0f, "manual motion was not restored after sweep completion");

    RunOptions interactive = SmallOptions(64u);
    interactive.sphereMotion = "sweep";
    Scene interactiveScene(interactive);
    interactiveScene.Update(nullptr, 1.0f / 60.0f, 3.5f);
    Check(Near(interactiveScene.MakeUniform(640u, 480u, 1.0f / 60.0f, 3.5f).collider.x,
               0.0f, 2.0e-4f),
          "interactive sweep did not start from time zero");

    ResetInput();
    Scene toggles(SmallOptions(64u));
    const uint32_t initial = toggles.MakeUniform(640u, 480u, 0.0f, 0.0f).flags.w;
    gKeys[GLFW_KEY_B] = GLFW_PRESS;
    toggles.Update(TestWindow(), 0.0f, 0.0f);
    const uint32_t bFirst = toggles.MakeUniform(640u, 480u, 0.0f, 0.0f).flags.w;
    toggles.Update(TestWindow(), 0.0f, 0.0f);
    const uint32_t bHeld = toggles.MakeUniform(640u, 480u, 0.0f, 0.0f).flags.w;
    Check((initial ^ bFirst) == BetterShading && bHeld == bFirst,
          "B is not an edge-triggered BetterShading toggle");
    gKeys[GLFW_KEY_B] = GLFW_RELEASE;
    toggles.Update(TestWindow(), 0.0f, 0.0f);
    gKeys[GLFW_KEY_B] = GLFW_PRESS;
    toggles.Update(TestWindow(), 0.0f, 0.0f);
    Check(toggles.MakeUniform(640u, 480u, 0.0f, 0.0f).flags.w == initial,
          "B did not toggle BetterShading back on after release");

    gKeys[GLFW_KEY_B] = GLFW_RELEASE;
    gKeys[GLFW_KEY_H] = GLFW_PRESS;
    toggles.Update(TestWindow(), 0.0f, 0.0f);
    const uint32_t hFirst = toggles.MakeUniform(640u, 480u, 0.0f, 0.0f).flags.w;
    toggles.Update(TestWindow(), 0.0f, 0.0f);
    const uint32_t hHeld = toggles.MakeUniform(640u, 480u, 0.0f, 0.0f).flags.w;
    Check((initial ^ hFirst) == ShowOverlay && hHeld == hFirst,
          "H is not an edge-triggered overlay toggle");
}

} // namespace

extern "C" void glfwGetCursorPos(GLFWwindow*, double* x, double* y) {
    if (x != nullptr) *x = gCursorX;
    if (y != nullptr) *y = gCursorY;
}

extern "C" int glfwGetMouseButton(GLFWwindow*, int button) {
    return button >= 0 && button <= GLFW_MOUSE_BUTTON_LAST ? gMouseButtons[button] : GLFW_RELEASE;
}

extern "C" int glfwGetKey(GLFWwindow*, int key) {
    return key >= 0 && key <= GLFW_KEY_LAST ? gKeys[key] : GLFW_RELEASE;
}

extern "C" void glfwSetWindowShouldClose(GLFWwindow*, int value) {
    gShouldClose = value;
}

int main() {
    try {
        TestCountsAndDefaultMesh();
        TestDeterminismAndClearance();
        TestFootprintsAndSphereArea();
        TestFieldSizesAndMillionTiming();
        TestBoundedFailure();
        TestCli();
        TestCamerasAndUniformContract();
        TestLightVolumeAndStability();
        TestSweepAndInputEdges();
        std::cout << "scene geometry/control CPU tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "scene geometry/control CPU test failed: " << error.what() << '\n';
        return 1;
    }
}
