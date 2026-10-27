
#pragma once

#include <glm/glm.hpp>
#include "Device.h"

struct CameraBufferObject {
  glm::mat4 viewMatrix;
  glm::mat4 projectionMatrix;
  glm::vec4 position;
  glm::vec4 frustumPlanes[6]; // normalized world planes: left/right/bottom/top/near/far
};
static_assert(sizeof(CameraBufferObject) == 240, "Camera std140 layout");

class Camera {
private:
    Device* device;
    
    CameraBufferObject cameraBufferObject;
    
    VkBuffer buffer;
    VkDeviceMemory bufferMemory;

    void* mappedData;

    float r, theta, phi;
    float viewTurn = 0.0f;
    void Upload();

public:
    Camera(Device* device, float aspectRatio);
    ~Camera();

    VkBuffer GetBuffer() const;
    
    void UpdateOrbit(float deltaX, float deltaY, float deltaZ);
    void SetOrbit(float yaw, float pitch, float radius);
    void SetViewTurn(float degrees);
    const CameraBufferObject& GetUniforms() const { return cameraBufferObject; }
    void UpdateAspectRatio(float aspectRatio);
};
