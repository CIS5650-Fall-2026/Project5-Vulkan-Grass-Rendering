
#pragma once

#include <glm/glm.hpp>
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
    
    void UpdateOrbit(float deltaX, float deltaY, float deltaZ);
    void UpdatePan(float deltaX, float deltaY, int viewportHeight);
    bool ScreenRayIntersectsSphere(double x, double y, int width, int height, const glm::vec3& center, float radius) const;
    bool ScreenToWorldOnPlane(double x, double y, int width, int height, float planeY, glm::vec3& worldPosition) const;
};
