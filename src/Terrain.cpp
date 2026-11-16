#include <glm/gtc/noise.hpp>
#include "Terrain.h"

namespace {
    constexpr float HILL_HEIGHT = 6.0f;
    constexpr float HILL_SCALE = 50.0f;
    constexpr float PATCH_SCALE = 18.0f;
    constexpr float SOIL_SCALE = 9.0f;

    float fbm(glm::vec2 p, int octaves) {
        float sum = 0.0f;
        float amplitude = 0.5f;
        for (int i = 0; i < octaves; ++i) {
            sum += amplitude * glm::perlin(p);
            // offset so the octaves don't share Perlin's zeros at integer lattice points
            p = p * 2.0f + glm::vec2(17.3f, -9.1f);
            amplitude *= 0.5f;
        }
        return sum;
    }

    glm::vec3 soilColor(glm::vec2 p, float lushness) {
        const glm::vec3 soilColor1(0.24f, 0.18f, 0.12f);
        const glm::vec3 soilColor2(0.45f, 0.36f, 0.24f);
        const glm::vec3 moss(0.27f, 0.31f, 0.15f);

        float n = fbm(p / SOIL_SCALE + glm::vec2(-23.1f, 5.7f), 3);
        glm::vec3 soil = glm::mix(soilColor1, soilColor2, glm::clamp(0.5f + n, 0.0f, 1.0f));
        return glm::mix(soil, moss, 0.6f * lushness);
    }
}

Terrain::Terrain(float size, int resolution)
    : size(size), resolution(resolution), cellSize(size / resolution) {
    int n = resolution + 1;
    heights.resize(n * n);
    lushness.resize(n * n);

    for (int j = 0; j < n; ++j) {
        for (int i = 0; i < n; ++i) {
            glm::vec2 p(-0.5f * size + i * cellSize, -0.5f * size + j * cellSize);
            heights[j * n + i] = HILL_HEIGHT * fbm(p / HILL_SCALE, 5);
            float patch = fbm(p / PATCH_SCALE + glm::vec2(31.7f, 47.2f), 3);
            lushness[j * n + i] = glm::clamp(0.5f + 1.2f * patch, 0.0f, 1.0f);
        }
    }
}

float Terrain::GetSize() const {
    return size;
}

float Terrain::HeightGrid(int i, int j) const {
    i = glm::clamp(i, 0, resolution);
    j = glm::clamp(j, 0, resolution);
    return heights[j * (resolution + 1) + i];
}

float Terrain::HeightAt(float x, float z) const {
    float gx = glm::clamp((x + 0.5f * size) / cellSize, 0.0f, resolution - 1e-4f);
    float gz = glm::clamp((z + 0.5f * size) / cellSize, 0.0f, resolution - 1e-4f);
    int i = static_cast<int>(gx);
    int j = static_cast<int>(gz);
    float fx = gx - i;
    float fz = gz - j;

    // same diagonal split as BuildMesh: corners a (i, j+1), b (i+1, j+1), c (i+1, j), d (i, j)
    float ha = HeightGrid(i, j + 1);
    float hb = HeightGrid(i + 1, j + 1);
    float hc = HeightGrid(i + 1, j);
    float hd = HeightGrid(i, j);
    if (fx + fz <= 1.0f) {
        return hd + fx * (hc - hd) + fz * (ha - hd);
    }
    return hb + (1.0f - fx) * (ha - hb) + (1.0f - fz) * (hc - hb);
}

float Terrain::LushnessAt(float x, float z) const {
    float gx = glm::clamp((x + 0.5f * size) / cellSize, 0.0f, resolution - 1e-4f);
    float gz = glm::clamp((z + 0.5f * size) / cellSize, 0.0f, resolution - 1e-4f);
    int i = static_cast<int>(gx);
    int j = static_cast<int>(gz);
    int n = resolution + 1;
    float bottom = glm::mix(lushness[j * n + i], lushness[j * n + i + 1], gx - i);
    float top = glm::mix(lushness[(j + 1) * n + i], lushness[(j + 1) * n + i + 1], gx - i);
    return glm::mix(bottom, top, gz - j);
}

void Terrain::BuildMesh(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices) const {
    int n = resolution + 1;
    vertices.resize(n * n);
    for (int j = 0; j < n; ++j) {
        for (int i = 0; i < n; ++i) {
            glm::vec2 p(-0.5f * size + i * cellSize, -0.5f * size + j * cellSize);
            float dx = HeightGrid(i + 1, j) - HeightGrid(i - 1, j);
            float dz = HeightGrid(i, j + 1) - HeightGrid(i, j - 1);
            glm::vec3 normal = glm::normalize(glm::vec3(-dx, 2.0f * cellSize, -dz));
            vertices[j * n + i] = { { p.x, heights[j * n + i], p.y }, soilColor(p, lushness[j * n + i]), normal };
        }
    }

    // counterclockwise seen from above
    indices.clear();
    indices.reserve(resolution * resolution * 6);
    for (int j = 0; j < resolution; ++j) {
        for (int i = 0; i < resolution; ++i) {
            uint32_t a = (j + 1) * n + i;
            uint32_t b = (j + 1) * n + i + 1;
            uint32_t c = j * n + i + 1;
            uint32_t d = j * n + i;
            indices.insert(indices.end(), { a, b, c, c, d, a });
        }
    }
}
