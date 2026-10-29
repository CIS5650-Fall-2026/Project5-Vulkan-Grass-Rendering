#include "Scene.h"
#include "BufferUtils.h"

#include <cstring>

namespace {
constexpr uint32_t initialColliderCount = 2;
}

Scene::Scene(Device* device) : device(device), colliders(new Colliders(device, initialColliderCount)) {
    BufferUtils::CreateBuffer(device, sizeof(Time), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, timeBuffer, timeBufferMemory);
    vkMapMemory(device->GetVkDevice(), timeBufferMemory, 0, sizeof(Time), 0, &mappedTimeData);
    memcpy(mappedTimeData, &time, sizeof(Time));
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

    time.deltaTime = nextDeltaTime.count();
    time.totalTime += time.deltaTime;

    memcpy(mappedTimeData, &time, sizeof(Time));
    colliders->Update(time.totalTime);
}

VkBuffer Scene::GetTimeBuffer() const {
    return timeBuffer;
}

uint32_t Scene::GetNumColliders() const {
    return colliders->GetNumColliders();
}

VkBuffer Scene::GetCollidersBuffer() const {
    return colliders->GetCollidersBuffer();
}

const Collider& Scene::GetCollider(uint32_t index) const {
    return colliders->GetCollider(index);
}


Scene::~Scene() {
    vkUnmapMemory(device->GetVkDevice(), timeBufferMemory);
    vkDestroyBuffer(device->GetVkDevice(), timeBuffer, nullptr);
    vkFreeMemory(device->GetVkDevice(), timeBufferMemory, nullptr);
    delete colliders;
}
