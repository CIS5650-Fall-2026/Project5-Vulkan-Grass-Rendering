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
    Upload();
}

void Camera::Upload() {
    cameraBufferObject.position = glm::inverse(cameraBufferObject.viewMatrix)[3];
    const glm::mat4 vp = cameraBufferObject.projectionMatrix * cameraBufferObject.viewMatrix;
    const auto row = [&](int i) { return glm::vec4(vp[0][i], vp[1][i], vp[2][i], vp[3][i]); };
    glm::vec4 planes[] = { row(3) + row(0), row(3) - row(0), row(3) + row(1),
        row(3) - row(1), row(2), row(3) - row(2) };
    for (int i = 0; i < 6; ++i)
        cameraBufferObject.frustumPlanes[i] = planes[i] / glm::length(glm::vec3(planes[i]));
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
    if (viewTurn != 0.0f) finalTransform = glm::rotate(finalTransform, glm::radians(viewTurn), glm::vec3(0, 1, 0));

    cameraBufferObject.viewMatrix = glm::inverse(finalTransform);

    Upload();
}

void Camera::UpdateAspectRatio(float aspectRatio) {
    cameraBufferObject.projectionMatrix = glm::perspective(glm::radians(45.0f), aspectRatio, 0.1f, 100.0f);
    cameraBufferObject.projectionMatrix[1][1] *= -1;
    Upload();
}

void Camera::SetViewTurn(float degrees) {
    viewTurn = degrees;
    UpdateOrbit(0.0f, 0.0f, 0.0f);
}

void Camera::SetOrbit(float yaw, float pitch, float radius) {
    theta = yaw;
    phi = pitch;
    r = radius;
    UpdateOrbit(0.0f, 0.0f, 0.0f);
}

Camera::~Camera() {
  vkUnmapMemory(device->GetVkDevice(), bufferMemory);
  vkDestroyBuffer(device->GetVkDevice(), buffer, nullptr);
  vkFreeMemory(device->GetVkDevice(), bufferMemory, nullptr);
}
