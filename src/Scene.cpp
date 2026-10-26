#include "Scene.h"
#include "BufferUtils.h"

Scene::Scene(Device* device) : device(device) {
    BufferUtils::CreateBuffer(device, sizeof(Time), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, timeBuffer, timeBufferMemory);
    vkMapMemory(device->GetVkDevice(), timeBufferMemory, 0, sizeof(Time), 0, &mappedData);
    memcpy(mappedData, &time, sizeof(Time));
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

void Scene::UpdateTime(float fixedDelta) {
    high_resolution_clock::time_point currentTime = high_resolution_clock::now();
    duration<float> nextDeltaTime = duration_cast<duration<float>>(currentTime - startTime);
    startTime = currentTime;

    time.deltaTime = time.simulate ? glm::clamp(fixedDelta > 0.0f ? fixedDelta : nextDeltaTime.count(), 0.0f, 1.0f / 30.0f) : 0.0f;
    time.totalTime += time.deltaTime;

    memcpy(mappedData, &time, sizeof(Time));
}

VkBuffer Scene::GetTimeBuffer() const {
    return timeBuffer;
}

Scene::~Scene() {
    vkUnmapMemory(device->GetVkDevice(), timeBufferMemory);
    vkDestroyBuffer(device->GetVkDevice(), timeBuffer, nullptr);
    vkFreeMemory(device->GetVkDevice(), timeBufferMemory, nullptr);
}

void Scene::SetSettings(uint32_t culling, bool simulate, float windStrength) {
    time.culling = culling;
    time.simulate = simulate ? 1 : 0;
    time.windStrength = windStrength;
    memcpy(mappedData, &time, sizeof(Time));
}

void Scene::ToggleCulling(uint32_t bit) {
    time.culling ^= bit;
}

void Scene::ToggleSimulation() {
    time.simulate = 1 - time.simulate;
}

void Scene::AdjustWind(float strengthChange, float angleChange) {
    time.windStrength = glm::clamp(time.windStrength + strengthChange, 0.0f, 100.0f);
    time.windAngle = glm::mod(time.windAngle + angleChange, 360.0f);
}

void Scene::MoveCollider(float x, float z) {
    time.collider.x = glm::clamp(time.collider.x + x, -7.0f, 7.0f);
    time.collider.z = glm::clamp(time.collider.z + z, -7.0f, 7.0f);
}

void Scene::SetInteraction(bool enabled, bool collision, bool occlusion) {
    time.colliderEnabled = enabled;
    time.collision = collision;
    time.occlusion = occlusion;
}

void Scene::ToggleOcclusion() {
    time.occlusion ^= 1;
}
