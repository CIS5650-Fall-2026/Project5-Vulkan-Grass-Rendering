#include "Scene.h"
#include "BufferUtils.h"
#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

Scene::Scene(Device* device, SimulationMode mode) : device(device), mode(mode) {
    simulation.physics = mode == SimulationMode::Physics ? 1u : 0u;
    BufferUtils::CreateBuffer(device, sizeof(SimulationUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, timeBuffer, timeBufferMemory);
    if (vkMapMemory(device->GetVkDevice(), timeBufferMemory, 0, sizeof(SimulationUniforms), 0, &mappedData) != VK_SUCCESS)
        throw std::runtime_error("Failed to map simulation uniforms");
    memcpy(mappedData, &simulation, sizeof(simulation));
    DiscardElapsedTime();
}
const std::vector<Model*>& Scene::GetModels() const { return models; }
const std::vector<Blades*>& Scene::GetBlades() const { return blades; }
void Scene::AddModel(Model* model) { models.push_back(model); }
void Scene::AddBlades(Blades* value) { blades.push_back(value); }
VkBuffer Scene::GetTimeBuffer() const { return timeBuffer; }

void Scene::Configure(double fixedDt, uint32_t forces, float wind, float gravity, bool startPaused) {
    fixedFrameSeconds = fixedDt;
    simulation.forceMask = forces;
    simulation.windStrength = wind;
    simulation.gravityStrength = gravity;
    SetPaused(startPaused);
}
void Scene::ConfigureCulling(uint32_t mask, uint32_t buckets, float threshold, float maxDistance, float tolerance) {
    SetCulling(mask);
    simulation.distanceBuckets = buckets;
    simulation.orientationThreshold = threshold;
    simulation.maxDistance = maxDistance;
    simulation.cullingSettings.x = tolerance;
}
void Scene::SetCulling(uint32_t mask) {
    // Static is the preserved Stage 1 reference path and bypasses compute entirely.
    simulation.cullingMask = mode == SimulationMode::Static ? 0u : mask;
}
void Scene::ConfigureLod(bool enabled, bool colors, float nearDistance, float farDistance, float maximum, float minimum) {
    if (!std::isfinite(nearDistance) || !std::isfinite(farDistance) || !std::isfinite(maximum) || !std::isfinite(minimum)
        || nearDistance < 0 || farDistance <= nearDistance || minimum < 2 || maximum > 64 || minimum > maximum)
        throw std::runtime_error("LOD requires 0 <= near < far and 2 <= min <= max <= 64");
    simulation.lodDistances = glm::vec4(nearDistance, farDistance, maximum, minimum);
    simulation.lodControls = glm::uvec4(enabled ? 1u : 0u, colors ? 1u : 0u, 0u, 0u);
}
void Scene::CycleCulling() {
    const uint32_t modes[] = { 0, FrustumCulling, OrientationCulling, DistanceCulling, AllCulling };
    for (size_t i = 0; i < 5; ++i) if (simulation.cullingMask == modes[i]) {
        SetCulling(modes[(i + 1) % 5]);
        return;
    }
    SetCulling(0);
}
void Scene::ConfigureInteraction(Model* model, bool enabled, bool visible, const glm::vec3& position,
    float radius, float strength, float speed, float travel, SphereMotion motion) {
    if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z)
        || !std::isfinite(radius) || radius < 0.1f || radius > 3.0f || position.y < radius
        || !std::isfinite(strength) || strength < 0 || strength > 120
        || !std::isfinite(speed) || speed < 0 || speed > 20 || !std::isfinite(travel) || travel < 0 || travel > 40)
        throw std::runtime_error("Invalid sphere configuration");
    sphereModel = model;
    sphereInitial = spherePosition = motionOrigin = position;
    sphereRadius = radius; sphereSpeed = speed; sphereTravel = travel;
    simulation.contactSettings.x = strength;
    simulation.contactControls = glm::uvec4(enabled ? 1u : 0u, visible ? 1u : 0u, static_cast<uint32_t>(motion), 0u);
    motionSteps = 0;
    UpdateInteraction();
}
void Scene::SetMovement(const glm::vec3& direction) {
    const float length = glm::length(direction);
    movement = length > 1.0f ? direction / length : direction;
}
void Scene::SetSphereMotion(SphereMotion motion) {
    simulation.contactControls.z = static_cast<uint32_t>(motion);
    motionOrigin = spherePosition;
    motionSteps = 0;
}
void Scene::ResetSphere() {
    spherePosition = motionOrigin = sphereInitial;
    motionSteps = 0;
}
void Scene::MoveSphereAway() {
    spherePosition = glm::vec3(20.0f, spherePosition.y, 0.0f);
    SetSphereMotion(SphereMotion::Manual);
}
void Scene::UpdateInteraction() {
    simulation.spherePath[0] = glm::vec4(spherePosition, sphereRadius);
    for (uint32_t i = 1; i <= simulation.stepCount; ++i) {
        ++motionSteps;
        const double time = double(motionSteps) / 120.0;
        const SphereMotion motion = static_cast<SphereMotion>(simulation.contactControls.z);
        if (motion == SphereMotion::Orbit) {
            const double radius = std::sqrt(double(motionOrigin.x) * motionOrigin.x + double(motionOrigin.z) * motionOrigin.z);
            if (radius > 0.01) {
                const double angle = std::atan2(double(motionOrigin.z), double(motionOrigin.x)) + time * sphereSpeed / radius;
                spherePosition = glm::vec3(float(radius * std::cos(angle)), motionOrigin.y, float(radius * std::sin(angle)));
            }
        } else if (motion == SphereMotion::Pass) {
            spherePosition = motionOrigin + glm::vec3(float(std::min(time * sphereSpeed, double(sphereTravel))), 0, 0);
        } else if (glm::dot(movement, movement) > 0.0f) {
            spherePosition += movement * sphereSpeed * simulation.stepSeconds;
            spherePosition = glm::clamp(spherePosition, glm::vec3(-10.0f, sphereRadius, -10.0f), glm::vec3(10.0f, 8.0f, 10.0f));
        }
        simulation.spherePath[i] = glm::vec4(spherePosition, sphereRadius);
    }
    for (uint32_t i = simulation.stepCount + 1; i < 9; ++i) simulation.spherePath[i] = glm::vec4(spherePosition, sphereRadius);
    if (sphereModel) sphereModel->SetTransform(glm::translate(glm::mat4(1), spherePosition)
        * glm::scale(glm::mat4(1), glm::vec3(simulation.contactControls.y ? sphereRadius : 0.0f)));
}
void Scene::DiscardElapsedTime() {
    lastWallTime = std::chrono::steady_clock::now();
    accumulator = 0.0;
}
void Scene::SetPaused(bool value) {
    paused = value;
    singleStepRequested = false;
    DiscardElapsedTime();
}
void Scene::Step() {
    SetPaused(true);
    singleStepRequested = true;
}
void Scene::Reset() {
    resetRequested = true;
    singleStepRequested = false;
    DiscardElapsedTime();
}
void Scene::ToggleForce(uint32_t force) { simulation.forceMask ^= force; }

void Scene::UpdateTime() {
    const auto now = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(now - lastWallTime).count();
    lastWallTime = now;
    simulation.reset = resetRequested ? 1u : 0u;
    simulation.stepCount = 0;
    if (resetRequested) {
        // Display the exact initial pose on the reset frame.
        simulationSteps = 0;
        accumulator = 0.0;
        resetRequested = false;
        ResetSphere();
    } else if (mode == SimulationMode::Physics) {
        if (singleStepRequested) {
            simulation.stepCount = 1;
            singleStepRequested = false;
        } else if (!paused) {
            if (fixedFrameSeconds > 0.0) elapsed = fixedFrameSeconds;
            if (elapsed < 0.0 || elapsed > 0.25) elapsed = 0.0;
            const double step = 1.0 / 120.0;
            accumulator = std::min(accumulator + elapsed, 8.0 * step);
            simulation.stepCount = static_cast<uint32_t>(std::floor((accumulator + 1e-10) / step));
            accumulator = std::max(0.0, accumulator - simulation.stepCount * step);
        }
    }
    simulation.startTime = static_cast<float>(GetSimulationTime());
    simulationSteps += simulation.stepCount;
    UpdateInteraction();
    memcpy(mappedData, &simulation, sizeof(simulation));
}
Scene::~Scene() {
    vkUnmapMemory(device->GetVkDevice(), timeBufferMemory);
    vkDestroyBuffer(device->GetVkDevice(), timeBuffer, nullptr);
    vkFreeMemory(device->GetVkDevice(), timeBufferMemory, nullptr);
}
