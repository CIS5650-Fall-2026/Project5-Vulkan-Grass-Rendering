#pragma once
#include "Model.h"
#include <cmath>

// Procedural unit sphere: no external model or additional dependency.
inline Model* CreateInteractionSphere(Device* device, VkCommandPool pool, VkImage texture) {
    const unsigned rings = 20, sectors = 32;
    const float pi = 3.14159265358979323846f;
    const glm::vec3 light = glm::normalize(glm::vec3(-0.5f, 1.0f, 0.7f));
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    for (unsigned ring = 0; ring <= rings; ++ring) {
        const float phi = pi * ring / rings;
        for (unsigned sector = 0; sector <= sectors; ++sector) {
            const float theta = 2.0f * pi * sector / sectors;
            const glm::vec3 normal(std::sin(phi) * std::cos(theta), std::cos(phi), std::sin(phi) * std::sin(theta));
            const float shade = 0.22f + 0.78f * glm::max(glm::dot(normal, light), 0.0f);
            vertices.push_back({normal, glm::vec3(1.0f, 0.32f, 0.07f) * shade,
                glm::vec2(float(sector) / sectors, float(ring) / rings)});
        }
    }
    for (unsigned ring = 0; ring < rings; ++ring) for (unsigned sector = 0; sector < sectors; ++sector) {
        const uint32_t a = ring * (sectors + 1) + sector, b = a + sectors + 1;
        indices.insert(indices.end(), {a, a + 1, b, a + 1, b + 1, b});
    }
    Model* sphere = new Model(device, pool, vertices, indices);
    sphere->SetTexture(texture); // Valid descriptor; the solid material does not sample it.
    sphere->SetVertexColorMaterial(true);
    return sphere;
}
