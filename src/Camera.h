
#pragma once

#include <glm/glm.hpp>
#include "Device.h"

struct CameraBufferObject {
  glm::mat4 viewMatrix;
  glm::mat4 projectionMatrix;
};

struct ViewportPose {
    glm::vec3 position;
    float yaw;            // about world +Y: 0 looks down -Z, positive turns toward +X
    float pitch;          // up from the horizon, stopping just short of the poles
    float pivotDistance;  // the orbit pivot's distance along the view axis
};

// Viewport camera that flies, orbits a pivot, pans and dollies.
class Camera {
private:
    Device* device;

    CameraBufferObject cameraBufferObject;

    VkBuffer buffer;
    VkDeviceMemory bufferMemory;

    void* mappedData;

    ViewportPose pose;
    ViewportPose homePose;
    float flySpeed;   // world units per second
    float pixelSize;  // view-plane height one pixel spans at distance 1

    glm::vec3 ViewDirection() const;
    glm::vec3 RightDirection() const;
    void TurnPose(double dx, double dy);
    void UpdateView();

public:
    Camera(Device* device, int width, int height);
    ~Camera();

    VkBuffer GetBuffer() const;

    void SetViewport(int width, int height);
    // pivot on target
    void SetHome(const glm::vec3& eye, const glm::vec3& target);
    void SetHome(const ViewportPose& home);
    void ResetToHome();
    const ViewportPose& GetPose() const;
    void SetFlySpeed(float unitsPerSecond);
    void ScaleFlySpeed(float wheelNotches);

    // dx, dy are mouse pixels
    void Turn(double dx, double dy);
    // turns, then moves back onto the line through the pivot, so the pivot stays put on screen
    void Orbit(double dx, double dy);
    // scales the pivot distance, so the camera never reaches the pivot
    void DollyToPivot(double dx, double dy);
    // one pixel's width at the pivot's distance per pixel
    void Pan(double dx, double dy);
    void MoveAlongGround(double dx, double dy);

    // forward, right, up are -1, 0 or 1 along the view, the camera's right and world +Y
    void Fly(int forward, int right, int up, float seconds);
    void MoveAlongView(float wheelNotches);
};
