#include "Scene.h"
#include "BufferUtils.h"
#include "ExperimentConfig.h"

Scene::Scene(Device* device) : device(device) {
    BufferUtils::CreateBuffer(device, sizeof(Time), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, timeBuffer, timeBufferMemory);
    vkMapMemory(device->GetVkDevice(), timeBufferMemory, 0, sizeof(Time), 0, &mappedData);
    memcpy(mappedData, &time, sizeof(Time));

    BufferUtils::CreateBuffer(device, sizeof(Sun), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, sunBuffer, sunBufferMemory);
    vkMapMemory(device->GetVkDevice(), sunBufferMemory, 0, sizeof(Sun), 0, &mappedSunData);
    UpdateSun();
}

const std::vector<Model*>& Scene::GetModels() const {
    return models;
}

const std::vector<Blades*>& Scene::GetBlades() const {
  return blades;
}

void Scene::AddModel(Model* model) {
    models.push_back(model);
}

void Scene::AddBlades(Blades* blades) {
  this->blades.push_back(blades);
}

void Scene::UpdateTime() {
    high_resolution_clock::time_point currentTime = high_resolution_clock::now();
    duration<float> nextDeltaTime = duration_cast<duration<float>>(currentTime - startTime);
    startTime = currentTime;

    time.deltaTime = ExperimentConfig::ENABLED ? ExperimentConfig::FIXED_DELTA_TIME : nextDeltaTime.count();
    time.totalTime += time.deltaTime;

    memcpy(mappedData, &time, sizeof(Time));
}

void Scene::UpdateSun() {
    const float dayDuration = 30.0f;
    const float pi = 3.14159265f;

    const float nightMargin = 0.25f;
    const float cycleArc = pi + 2.0f * nightMargin;

    float sunTime = ExperimentConfig::ENABLED ? ExperimentConfig::SUN_TIME : time.totalTime;
    float angle = std::fmod(sunTime * (cycleArc / dayDuration) + 0.25f + nightMargin, cycleArc) - nightMargin;

    glm::vec3 direction(0.0f, std::sin(angle), -std::cos(angle));

    float intensity = glm::smoothstep(0.0f, 0.10f, direction.y);
    float daylight = glm::smoothstep(-0.15f, 0.15f, direction.y);
    float warmth = glm::smoothstep(0.0f, 0.40f, direction.y);

    glm::vec3 color = glm::mix(
        glm::vec3(1.0f, 0.45f, 0.18f),
        glm::vec3(1.0f, 0.98f, 0.90f),
        warmth
    );

    sun.direction = glm::vec4(direction, intensity);
    sun.color = glm::vec4(color, daylight);

    memcpy(mappedSunData, &sun, sizeof(Sun));
}

VkBuffer Scene::GetTimeBuffer() const {
    return timeBuffer;
}

VkBuffer Scene::GetSunBuffer() const {
    return sunBuffer;
}

Scene::~Scene() {
    vkUnmapMemory(device->GetVkDevice(), timeBufferMemory);
    vkDestroyBuffer(device->GetVkDevice(), timeBuffer, nullptr);
    vkFreeMemory(device->GetVkDevice(), timeBufferMemory, nullptr);

    vkUnmapMemory(device->GetVkDevice(), sunBufferMemory);
    vkDestroyBuffer(device->GetVkDevice(), sunBuffer, nullptr);
    vkFreeMemory(device->GetVkDevice(), sunBufferMemory, nullptr);
}
