#include <iostream>

#define GLM_FORCE_RADIANS
// Use Vulkan depth range of 0.0 to 1.0 instead of OpenGL
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/gtc/matrix_transform.hpp>

#include "Camera.h"
#include "BufferUtils.h"

Camera::Camera(Device* device, float aspectRatio) : device(device) {
    r = 10.0f;
    theta = 0.0f;
    phi = 0.0f;
    cameraBufferObject.viewMatrix = glm::lookAt(glm::vec3(0.0f, 1.0f, 10.0f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    cameraBufferObject.projectionMatrix = glm::perspective(glm::radians(45.0f), aspectRatio, 0.1f, 100.0f);
    cameraBufferObject.projectionMatrix[1][1] *= -1; // y-coordinate is flipped

    BufferUtils::CreateBuffer(device, sizeof(CameraBufferObject), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, buffer, bufferMemory);
    vkMapMemory(device->GetVkDevice(), bufferMemory, 0, sizeof(CameraBufferObject), 0, &mappedData);
    memcpy(mappedData, &cameraBufferObject, sizeof(CameraBufferObject));
}

VkBuffer Camera::GetBuffer() const {
    return buffer;
}

void Camera::UpdateOrbit(float deltaX, float deltaY, float deltaZ) {
    theta += deltaX;
    phi += deltaY;
    r = glm::clamp(r - deltaZ, 1.0f, 50.0f);

    float radTheta = glm::radians(theta);
    float radPhi = glm::radians(phi);

    glm::mat4 rotation = glm::rotate(glm::mat4(1.0f), radTheta, glm::vec3(0.0f, 1.0f, 0.0f)) * glm::rotate(glm::mat4(1.0f), radPhi, glm::vec3(1.0f, 0.0f, 0.0f));
    glm::mat4 finalTransform = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f)) * rotation * glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 1.0f, r));

    cameraBufferObject.viewMatrix = glm::inverse(finalTransform);

    memcpy(mappedData, &cameraBufferObject, sizeof(CameraBufferObject));
}

void Camera::UpdatePan(float deltaX, float deltaY, int viewportHeight) {
    if (viewportHeight <= 0) {
        return;
    }

    const glm::mat4 inverseView = glm::inverse(cameraBufferObject.viewMatrix);
    const glm::vec3 right(inverseView[0]);
    const glm::vec3 up(inverseView[1]);
    const float worldUnitsPerPixel = 2.0f * r * glm::tan(glm::radians(22.5f)) / viewportHeight;
    const glm::vec3 pan = worldUnitsPerPixel * (-deltaX * right + deltaY * up);

    glm::mat4 rotation = glm::rotate(glm::mat4(1.0f), glm::radians(theta), glm::vec3(0.0f, 1.0f, 0.0f)) *
        glm::rotate(glm::mat4(1.0f), glm::radians(phi), glm::vec3(1.0f, 0.0f, 0.0f));
    glm::mat4 finalTransform = glm::translate(glm::mat4(1.0f), pan) * rotation *
        glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 1.0f, r));
    cameraBufferObject.viewMatrix = glm::inverse(finalTransform);

    memcpy(mappedData, &cameraBufferObject, sizeof(CameraBufferObject));
}

bool Camera::ScreenToWorldOnPlane(double x, double y, int width, int height, float planeY, glm::vec3& worldPosition) const {
    if (width <= 0 || height <= 0) {
        return false;
    }

    const float ndcX = static_cast<float>(2.0 * x / width - 1.0);
    const float ndcY = static_cast<float>(2.0 * y / height - 1.0);
    const glm::mat4 inverseViewProjection = glm::inverse(cameraBufferObject.projectionMatrix * cameraBufferObject.viewMatrix);
    glm::vec4 nearPoint = inverseViewProjection * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
    glm::vec4 farPoint = inverseViewProjection * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
    nearPoint /= nearPoint.w;
    farPoint /= farPoint.w;

    const glm::vec3 rayOrigin(nearPoint);
    const glm::vec3 rayDirection = glm::normalize(glm::vec3(farPoint - nearPoint));
    if (glm::abs(rayDirection.y) < 0.0001f) {
        return false;
    }

    const float distance = (planeY - rayOrigin.y) / rayDirection.y;
    if (distance < 0.0f) {
        return false;
    }

    worldPosition = rayOrigin + rayDirection * distance;
    return true;
}

bool Camera::ScreenRayIntersectsSphere(double x, double y, int width, int height, const glm::vec3& center, float radius) const {
    glm::vec3 worldPosition;
    if (!ScreenToWorldOnPlane(x, y, width, height, center.y, worldPosition)) {
        return false;
    }

    const glm::mat4 inverseView = glm::inverse(cameraBufferObject.viewMatrix);
    const glm::vec3 cameraPosition(inverseView * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
    const glm::vec3 rayDirection = glm::normalize(worldPosition - cameraPosition);
    const glm::vec3 toSphere = center - cameraPosition;
    const float projection = glm::dot(toSphere, rayDirection);
    const float perpendicularDistanceSquared = glm::dot(toSphere, toSphere) - projection * projection;
    return projection >= 0.0f && perpendicularDistanceSquared <= radius * radius;
}

Camera::~Camera() {
  vkUnmapMemory(device->GetVkDevice(), bufferMemory);
  vkDestroyBuffer(device->GetVkDevice(), buffer, nullptr);
  vkFreeMemory(device->GetVkDevice(), bufferMemory, nullptr);
}
