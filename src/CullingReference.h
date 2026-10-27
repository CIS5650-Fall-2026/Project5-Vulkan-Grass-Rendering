#pragma once

#include "Renderer.h"
#include <algorithm>
#include <cmath>

// Diagnostic CPU oracle. Rendering never calls this path unless a readback is requested.
// Return the first rejection reason: 0 keep, 1 frustum, 2 orientation, 3 distance.
inline unsigned CullingReference(const Blade& b, uint32_t id, const GrassSnapshot& snapshot, bool* orientationBoundary = nullptr) {
    if (orientationBoundary) *orientationBoundary = false;
    const auto& settings = snapshot.simulation;
    if (settings.cullingMask == 0) return 0;
    glm::dvec3 up = glm::normalize(glm::dvec3(b.up));
    const glm::dvec3 ref = std::abs(up.y) < .99 ? glm::dvec3(0, 1, 0) : glm::dvec3(1, 0, 0);
    const glm::dvec3 tangent = glm::normalize(ref - glm::dot(ref, up) * up);
    const glm::dvec3 side = std::cos(double(b.v0.w)) * tangent + std::sin(double(b.v0.w)) * glm::cross(tangent, up);
    const glm::dmat4 model(snapshot.model);
    const glm::dvec3 p0(model * glm::dvec4(glm::dvec3(b.v0), 1));
    const glm::dvec3 p1(model * glm::dvec4(glm::dvec3(b.v1), 1));
    const glm::dvec3 p2(model * glm::dvec4(glm::dvec3(b.v2), 1));
    const glm::dvec3 width = glm::dmat3(model) * (side * double(b.v2.w));
    const glm::dvec3 mid = .25 * p0 + .5 * p1 + .25 * p2;
    if (settings.cullingMask & FrustumCulling) {
        // Test the complete ribbon control hull directly, in double precision.
        for (const auto& value : snapshot.camera.frustumPlanes) {
            const glm::dvec4 plane(value);
            const double w = std::abs(glm::dot(glm::dvec3(plane), width));
            const double farthest = std::max(glm::dot(plane, glm::dvec4(p0, 1)) + .5 * w,
                std::max(glm::dot(plane, glm::dvec4(p1, 1)) + .25 * w, glm::dot(plane, glm::dvec4(p2, 1))));
            if (farthest < -double(settings.cullingSettings.x)) return 1;
        }
    }
    const glm::dvec3 position(snapshot.camera.position);
    if (settings.cullingMask & OrientationCulling) {
        const glm::dvec3 view = position - mid;
        const double denominator = glm::length(view) * glm::length(width);
        const double alignment = denominator > 1e-12 ? std::abs(glm::dot(view, width) / denominator) : 0;
        // GPU normalize/trigonometry are float32; classify this narrow boundary separately.
        if (orientationBoundary) *orientationBoundary = std::abs(alignment - settings.orientationThreshold) <= 2e-6;
        if (alignment > settings.orientationThreshold) return 2;
    }
    if (settings.cullingMask & DistanceCulling) {
        up = glm::normalize(glm::dmat3(model) * up);
        const glm::dvec3 offset = p0 - position;
        const double distance = glm::length(offset - up * glm::dot(offset, up));
        if (distance >= settings.maxDistance) {
            if (orientationBoundary) *orientationBoundary = false;
            return 3;
        }
        const uint32_t bucket = std::min(uint32_t(distance / settings.maxDistance * settings.distanceBuckets), settings.distanceBuckets - 1);
        uint32_t hash = id;
        hash ^= hash >> 16; hash *= 0x7feb352du;
        hash ^= hash >> 15; hash *= 0x846ca68bu;
        hash ^= hash >> 16;
        if (hash % settings.distanceBuckets >= settings.distanceBuckets - bucket) {
            if (orientationBoundary) *orientationBoundary = false;
            return 3;
        }
    }
    return 0;
}
