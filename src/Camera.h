
#pragma once

#include <glm/glm.hpp>
#include <string>
#include "Device.h"

struct CameraBufferObject {
  glm::mat4 viewMatrix;
  glm::mat4 projectionMatrix;
};

class Camera {
private:
    Device* device;
    
    CameraBufferObject cameraBufferObject;
    
    VkBuffer buffer;
    VkDeviceMemory bufferMemory;

    void* mappedData;

    float r, theta, phi;

public:
    Camera(Device* device, float aspectRatio);
    ~Camera();

    VkBuffer GetBuffer() const;
    const CameraBufferObject& GetState() const { return cameraBufferObject; }
    
    void UpdateOrbit(float deltaX, float deltaY, float deltaZ);
    void UpdateAspectRatio(float aspectRatio);
    void SetView(const std::string& name);
};
