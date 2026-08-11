#pragma once
#include "raylib.h"
#include <vector>

class GroundGrid {
public:
    void AddMesh(const Mesh& mesh, Matrix transform);
    void Finish();
    void Clear();

    bool Empty() const { return tris_.empty(); }
    int  TriangleCount() const { return (int)(tris_.size() / 3); }
    bool HeightAt(float x, float z, float& outY) const;
    bool HeightNear(float x, float z, float refY, float& outY) const;

private:
    std::vector<Vector3> tris_;    std::vector<std::vector<int>> cells_;    float minX_ = 0, minZ_ = 0, cell_ = 64.0f;
    int nx_ = 0, nz_ = 0;

    const std::vector<int>* CellAt(float x, float z) const;
};
