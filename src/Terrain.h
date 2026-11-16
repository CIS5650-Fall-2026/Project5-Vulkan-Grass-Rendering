#pragma once

#include <vector>
#include <glm/glm.hpp>
#include "Vertex.h"

class Terrain {
public:
    Terrain(float size, int resolution);

    float GetSize() const;

    // height of the rendered triangle surface, so blades sit exactly on the mesh
    float HeightAt(float x, float z) const;

    // 0 where grass is shortest and sparsest, 1 where it is fullest
    float LushnessAt(float x, float z) const;

    void BuildMesh(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices) const;

private:
    float size;
    int resolution;  // cells per side
    float cellSize;

    // (resolution + 1)^2 grid points, row-major with z as the row
    std::vector<float> heights;
    std::vector<float> lushness;

    float HeightGrid(int i, int j) const;
};
