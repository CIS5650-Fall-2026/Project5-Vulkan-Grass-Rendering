#include <cmath>
#include <iostream>

#define GLM_FORCE_RADIANS
// Use Vulkan depth range of 0.0 to 1.0 instead of OpenGL
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/gtc/matrix_transform.hpp>

#include "Camera.h"
#include "BufferUtils.h"

namespace {
    const float PI = 3.14159265f;
    const float FOVY = 45.0f * PI / 180.0f;
    const float PITCH_LIMIT = 0.5f * PI - 0.001f;
    // mouse look and orbit, radians per pixel of mouse motion
    const float TURN_PER_PIXEL = 0.0025f;
    // LMB drag: a pixel of vertical motion moves as far as this much flying
    const float GROUND_SECONDS_PER_PIXEL = 0.005f;
    // Alt + RMB drag: the distance to the pivot scales by e^-x for x pixels
    const float DOLLY_PER_PIXEL = 0.005f;
    // RMB + wheel scales the fly speed by this per notch
    const float SPEED_STEP = 1.25f;
    // a wheel notch moves as far as this much flying
    const float WHEEL_STEP_SECONDS = 0.25f;
}

Camera::Camera(Device* device, int width, int height) : device(device) {
    pose = { glm::vec3(0.0f, 1.0f, 10.0f), 0.0f, 0.0f, 10.0f };
    homePose = pose;
    flySpeed = 10.0f;

    BufferUtils::CreateBuffer(device, sizeof(CameraBufferObject), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, buffer, bufferMemory);
    vkMapMemory(device->GetVkDevice(), bufferMemory, 0, sizeof(CameraBufferObject), 0, &mappedData);
    SetViewport(width, height);
}

VkBuffer Camera::GetBuffer() const {
    return buffer;
}

void Camera::SetViewport(int width, int height) {
    cameraBufferObject.projectionMatrix = glm::perspective(FOVY, static_cast<float>(width) / height, 0.1f, 300.0f);
    cameraBufferObject.projectionMatrix[1][1] *= -1; // y-coordinate is flipped
    pixelSize = 2.0f * std::tan(0.5f * FOVY) / height;
    UpdateView();
}

glm::vec3 Camera::ViewDirection() const {
    return glm::vec3(std::sin(pose.yaw) * std::cos(pose.pitch), std::sin(pose.pitch), -std::cos(pose.yaw) * std::cos(pose.pitch));
}

glm::vec3 Camera::RightDirection() const {
    return glm::normalize(glm::cross(ViewDirection(), glm::vec3(0.0f, 1.0f, 0.0f)));
}

void Camera::UpdateView() {
    cameraBufferObject.viewMatrix = glm::lookAt(pose.position, pose.position + ViewDirection(), glm::vec3(0.0f, 1.0f, 0.0f));
    memcpy(mappedData, &cameraBufferObject, sizeof(CameraBufferObject));
}

void Camera::SetHome(const glm::vec3& eye, const glm::vec3& target) {
    glm::vec3 view = glm::normalize(target - eye);
    homePose.position = eye;
    homePose.yaw = std::atan2(view.x, -view.z);
    homePose.pitch = glm::clamp(std::asin(glm::clamp(view.y, -1.0f, 1.0f)), -PITCH_LIMIT, PITCH_LIMIT);
    homePose.pivotDistance = glm::length(target - eye);
    ResetToHome();
}

void Camera::SetHome(const ViewportPose& home) {
    homePose = home;
    homePose.pitch = glm::clamp(homePose.pitch, -PITCH_LIMIT, PITCH_LIMIT);
    ResetToHome();
}

void Camera::ResetToHome() {
    pose = homePose;
    UpdateView();
}

const ViewportPose& Camera::GetPose() const {
    return pose;
}

void Camera::SetFlySpeed(float unitsPerSecond) {
    flySpeed = unitsPerSecond;
}

void Camera::ScaleFlySpeed(float wheelNotches) {
    flySpeed *= std::pow(SPEED_STEP, wheelNotches);
}

void Camera::TurnPose(double dx, double dy) {
    pose.yaw = std::remainder(pose.yaw + TURN_PER_PIXEL * static_cast<float>(dx), 2.0f * PI);
    pose.pitch = glm::clamp(pose.pitch - TURN_PER_PIXEL * static_cast<float>(dy), -PITCH_LIMIT, PITCH_LIMIT);
}

void Camera::Turn(double dx, double dy) {
    TurnPose(dx, dy);
    UpdateView();
}

void Camera::Orbit(double dx, double dy) {
    glm::vec3 pivot = pose.position + pose.pivotDistance * ViewDirection();
    TurnPose(dx, dy);
    pose.position = pivot - pose.pivotDistance * ViewDirection();
    UpdateView();
}

void Camera::DollyToPivot(double dx, double dy) {
    glm::vec3 pivot = pose.position + pose.pivotDistance * ViewDirection();
    pose.pivotDistance *= std::exp(-DOLLY_PER_PIXEL * static_cast<float>(dx - dy));
    pose.position = pivot - pose.pivotDistance * ViewDirection();
    UpdateView();
}

void Camera::Pan(double dx, double dy) {
    glm::vec3 right = RightDirection();
    glm::vec3 up = glm::cross(right, ViewDirection());
    float perPixel = pose.pivotDistance * pixelSize;
    pose.position += perPixel * (static_cast<float>(dx) * right - static_cast<float>(dy) * up);
    UpdateView();
}

void Camera::MoveAlongGround(double dx, double dy) {
    TurnPose(dx, 0.0);
    glm::vec3 heading(std::sin(pose.yaw), 0.0f, -std::cos(pose.yaw));
    pose.position -= static_cast<float>(dy) * flySpeed * GROUND_SECONDS_PER_PIXEL * heading;
    UpdateView();
}

void Camera::Fly(int forward, int right, int up, float seconds) {
    glm::vec3 direction = static_cast<float>(forward) * ViewDirection() + static_cast<float>(right) * RightDirection()
        + static_cast<float>(up) * glm::vec3(0.0f, 1.0f, 0.0f);
    if (direction == glm::vec3(0.0f)) {
        return;
    }
    pose.position += flySpeed * seconds * glm::normalize(direction);
    UpdateView();
}

void Camera::MoveAlongView(float wheelNotches) {
    pose.position += wheelNotches * flySpeed * WHEEL_STEP_SECONDS * ViewDirection();
    UpdateView();
}

Camera::~Camera() {
  vkUnmapMemory(device->GetVkDevice(), bufferMemory);
  vkDestroyBuffer(device->GetVkDevice(), buffer, nullptr);
  vkFreeMemory(device->GetVkDevice(), bufferMemory, nullptr);
}
