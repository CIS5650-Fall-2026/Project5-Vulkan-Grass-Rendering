#include "Scene.h"
#include "BufferUtils.h"
#include <glm/gtc/matrix_transform.hpp>

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

void Scene::UpdateTime() {
    high_resolution_clock::time_point currentTime = high_resolution_clock::now();
    duration<float> nextDeltaTime = duration_cast<duration<float>>(currentTime - startTime);
    startTime = currentTime;

    time.deltaTime = glm::min(nextDeltaTime.count(), 0.033f);
    time.totalTime += time.deltaTime;

    if (!colliderDragging) {
        time.colliderVelocity.y -= 9.8f * time.deltaTime;
        time.collider.x += time.colliderVelocity.x * time.deltaTime;
        time.collider.y += time.colliderVelocity.y * time.deltaTime;
        time.collider.z += time.colliderVelocity.z * time.deltaTime;

        const float groundHeight = time.collider.w;
        if (time.collider.y < groundHeight) {
            time.collider.y = groundHeight;
            if (time.colliderVelocity.y < 0.0f) {
                time.colliderVelocity.y *= -0.35f;
            }
            time.colliderVelocity.x *= 0.82f;
            time.colliderVelocity.z *= 0.82f;
            if (glm::abs(time.colliderVelocity.y) < 0.2f) {
                time.colliderVelocity.y = 0.0f;
            }
        }

        const float maxPosition = 7.5f - time.collider.w;
        if (time.collider.x < -maxPosition || time.collider.x > maxPosition) {
            time.collider.x = glm::clamp(time.collider.x, -maxPosition, maxPosition);
            time.colliderVelocity.x *= -0.4f;
        }
        if (time.collider.z < -maxPosition || time.collider.z > maxPosition) {
            time.collider.z = glm::clamp(time.collider.z, -maxPosition, maxPosition);
            time.colliderVelocity.z *= -0.4f;
        }
        if (colliderModel != nullptr) {
            colliderModel->SetTransform(glm::translate(glm::mat4(1.0f), glm::vec3(time.collider)) *
                glm::scale(glm::mat4(1.0f), glm::vec3(time.collider.w)));
        }
    }
    memcpy(mappedData, &time, sizeof(Time));
}

void Scene::AdjustWind(float strengthDelta, float directionDelta) {
    time.windStrength = glm::clamp(time.windStrength + strengthDelta, 0.0f, 6.0f);
    time.windDirection += directionDelta;
    const float fullTurn = 6.28318530718f;
    if (time.windDirection >= fullTurn) {
        time.windDirection -= fullTurn;
    } else if (time.windDirection < 0.0f) {
        time.windDirection += fullTurn;
    }
}

void Scene::ResetWind() {
    time.windStrength = 1.0f;
    time.windDirection = 0.0f;
}

void Scene::SetColliderModel(Model* model) {
    colliderModel = model;
    colliderModel->SetTransform(glm::translate(glm::mat4(1.0f), glm::vec3(time.collider)) *
        glm::scale(glm::mat4(1.0f), glm::vec3(time.collider.w)));
}

void Scene::MoveCollider(float deltaX, float deltaZ) {
    const float maxPosition = 7.5f - time.collider.w;
    time.collider.x = glm::clamp(time.collider.x + deltaX, -maxPosition, maxPosition);
    time.collider.z = glm::clamp(time.collider.z + deltaZ, -maxPosition, maxPosition);
    if (colliderModel != nullptr) {
        colliderModel->SetTransform(glm::translate(glm::mat4(1.0f), glm::vec3(time.collider)) *
            glm::scale(glm::mat4(1.0f), glm::vec3(time.collider.w)));
    }
    memcpy(mappedData, &time, sizeof(Time));
}

glm::vec3 Scene::GetColliderPosition() const {
    return glm::vec3(time.collider);
}

float Scene::GetColliderRadius() const {
    return time.collider.w;
}

Model* Scene::GetColliderModel() const {
    return colliderModel;
}

void Scene::BeginColliderDrag() {
    colliderDragging = true;
    time.colliderVelocity = glm::vec4(0.0f);
}

void Scene::DragColliderTo(const glm::vec3& position) {
    const float maxPosition = 7.5f - time.collider.w;
    time.collider.x = glm::clamp(position.x, -maxPosition, maxPosition);
    time.collider.z = glm::clamp(position.z, -maxPosition, maxPosition);
    time.colliderVelocity = glm::vec4(0.0f);
    if (colliderModel != nullptr) {
        colliderModel->SetTransform(glm::translate(glm::mat4(1.0f), glm::vec3(time.collider)) *
            glm::scale(glm::mat4(1.0f), glm::vec3(time.collider.w)));
    }
}

void Scene::EndColliderDrag() {
    colliderDragging = false;
}

void Scene::SetActiveBladeCount(uint32_t count) {
    time.performanceSettings.x = glm::clamp(count, 1u, NUM_BLADES);
    memcpy(mappedData, &time, sizeof(Time));
}

void Scene::ToggleCullingTest(uint32_t test) {
    time.performanceSettings.y ^= test;
    memcpy(mappedData, &time, sizeof(Time));
}

uint32_t Scene::GetActiveBladeCount() const {
    return time.performanceSettings.x;
}

uint32_t Scene::GetCullingMask() const {
    return time.performanceSettings.y;
}

VkBuffer Scene::GetTimeBuffer() const {
    return timeBuffer;
}

Scene::~Scene() {
    vkUnmapMemory(device->GetVkDevice(), timeBufferMemory);
    vkDestroyBuffer(device->GetVkDevice(), timeBuffer, nullptr);
    vkFreeMemory(device->GetVkDevice(), timeBufferMemory, nullptr);
}
