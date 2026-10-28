#pragma once

#include "AppConfig.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

struct GLFWwindow;

class Scene {
public:
    Scene(const RunOptions& options);

    void Update(GLFWwindow* window, float delta, float simTime);
    FrameUniform MakeUniform(uint32_t width, uint32_t height, float delta, float simTime) const;
    const std::vector<Blade>& Blades() const;
    const std::vector<SceneVertex>& Vertices() const;
    const std::vector<uint32_t>& Indices() const;
    const std::vector<SceneDraw>& Draws() const;
    float StaticObstacleDistance(const glm::vec3& position) const;
    bool IsPaused() const;
    bool ConsumeResetRequested();

private:
    struct StaticFootprint {
        std::vector<glm::vec2> hull;
        glm::vec2 minimum{0.0f};
        glm::vec2 maximum{0.0f};
    };

    void GenerateBlades();
    void GenerateGeometry();
    void RecordStaticFootprint(std::size_t firstVertex);
    void BuildLightMatrix();
    void ConfigureCamera(const std::string& preset);
    glm::vec3 CameraPosition() const;

    friend struct SceneGeometryTestAccess;

    RunOptions options_;
    std::vector<Blade> blades_;
    std::vector<SceneVertex> vertices_;
    std::vector<uint32_t> indices_;
    std::vector<SceneDraw> draws_;
    std::vector<StaticFootprint> staticFootprints_;

    glm::vec3 cameraTarget_{0.0f, 1.0f, -2.0f};
    float cameraYaw_ = 0.72f;
    float cameraPitch_ = 0.22f;
    float cameraDistance_ = 35.0f;

    glm::mat4 lightViewProj_{1.0f};
    glm::vec3 sunDirection_{0.0f, 1.0f, 0.0f};
    glm::vec3 colliderCenter_{0.0f, 1.0f, 0.0f};
    glm::vec3 wind_{4.0f, 0.0f, 1.0f};
    float colliderRadius_ = 1.0f;
    float windStrength_ = 0.0f;
    float windAngle_ = 0.0f;
    float gustFrequency_ = 1.0f;

    uint32_t cullMask_ = 0;
    uint32_t featureMask_ = 0;
    bool lodEnabled_ = false;
    bool paused_ = false;
    bool resetRequested_ = false;
    bool sweepFinished_ = false;

    std::array<bool, 10> toggleKeyDown_{};
    bool cursorInitialized_ = false;
    double previousCursorX_ = 0.0;
    double previousCursorY_ = 0.0;
};
