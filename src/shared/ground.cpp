#include "ground.h"
#include "raymath.h"
#include <math.h>
#include <float.h>

void GroundGrid::Clear() {
    tris_.clear();
    cells_.clear();
    nx_ = nz_ = 0;
}

void GroundGrid::AddMesh(const Mesh& mesh, Matrix transform) {
    if (!mesh.vertices || mesh.vertexCount <= 0) return;
    const int triCount = mesh.triangleCount;
    tris_.reserve(tris_.size() + (size_t)triCount * 3);

    for (int t = 0; t < triCount; t++) {
        for (int k = 0; k < 3; k++) {
            int vi = mesh.indices ? (int)mesh.indices[t * 3 + k] : (t * 3 + k);
            if (vi < 0 || vi >= mesh.vertexCount) return;
            Vector3 p = { mesh.vertices[vi * 3 + 0], mesh.vertices[vi * 3 + 1], mesh.vertices[vi * 3 + 2] };
            tris_.push_back(Vector3Transform(p, transform));
        }
    }
}

void GroundGrid::Finish() {
    cells_.clear();
    nx_ = nz_ = 0;
    if (tris_.empty()) return;

    float minX = FLT_MAX, minZ = FLT_MAX, maxX = -FLT_MAX, maxZ = -FLT_MAX;
    for (const Vector3& p : tris_) {
        if (p.x < minX) minX = p.x;
        if (p.x > maxX) maxX = p.x;
        if (p.z < minZ) minZ = p.z;
        if (p.z > maxZ) maxZ = p.z;
    }
    const int TARGET = 64;
    float spanX = maxX - minX, spanZ = maxZ - minZ;
    cell_ = fmaxf(fmaxf(spanX, spanZ) / TARGET, 8.0f);
    minX_ = minX;
    minZ_ = minZ;
    nx_ = (int)(spanX / cell_) + 1;
    nz_ = (int)(spanZ / cell_) + 1;
    if (nx_ < 1) nx_ = 1;
    if (nz_ < 1) nz_ = 1;
    cells_.assign((size_t)nx_ * nz_, {});

    const int triCount = (int)(tris_.size() / 3);
    for (int t = 0; t < triCount; t++) {
        const Vector3& a = tris_[t * 3 + 0];
        const Vector3& b = tris_[t * 3 + 1];
        const Vector3& c = tris_[t * 3 + 2];
        float tx0 = fminf(a.x, fminf(b.x, c.x)), tx1 = fmaxf(a.x, fmaxf(b.x, c.x));
        float tz0 = fminf(a.z, fminf(b.z, c.z)), tz1 = fmaxf(a.z, fmaxf(b.z, c.z));
        int i0 = (int)((tx0 - minX_) / cell_), i1 = (int)((tx1 - minX_) / cell_);
        int j0 = (int)((tz0 - minZ_) / cell_), j1 = (int)((tz1 - minZ_) / cell_);
        if (i0 < 0) i0 = 0; if (j0 < 0) j0 = 0;
        if (i1 >= nx_) i1 = nx_ - 1; if (j1 >= nz_) j1 = nz_ - 1;
        for (int j = j0; j <= j1; j++)
            for (int i = i0; i <= i1; i++)
                cells_[(size_t)j * nx_ + i].push_back(t);
    }
}

const std::vector<int>* GroundGrid::CellAt(float x, float z) const {
    if (cells_.empty()) return nullptr;
    int i = (int)((x - minX_) / cell_);
    int j = (int)((z - minZ_) / cell_);
    if (i < 0 || j < 0 || i >= nx_ || j >= nz_) return nullptr;
    return &cells_[(size_t)j * nx_ + i];
}
static bool TriHeight(const Vector3& a, const Vector3& b, const Vector3& c,
                      float x, float z, float& outY) {
    const float d = (b.z - c.z) * (a.x - c.x) + (c.x - b.x) * (a.z - c.z);
    if (fabsf(d) < 1e-9f) return false;    const float w0 = ((b.z - c.z) * (x - c.x) + (c.x - b.x) * (z - c.z)) / d;
    const float w1 = ((c.z - a.z) * (x - c.x) + (a.x - c.x) * (z - c.z)) / d;
    const float w2 = 1.0f - w0 - w1;
    const float EPS = -0.0005f;    if (w0 < EPS || w1 < EPS || w2 < EPS) return false;
    outY = w0 * a.y + w1 * b.y + w2 * c.y;
    return true;
}

bool GroundGrid::HeightAt(float x, float z, float& outY) const {
    const std::vector<int>* cell = CellAt(x, z);
    if (!cell) return false;
    bool found = false;
    float best = -FLT_MAX;
    for (int t : *cell) {
        float y;
        if (TriHeight(tris_[t * 3], tris_[t * 3 + 1], tris_[t * 3 + 2], x, z, y)) {
            if (!found || y > best) { best = y; found = true; }
        }
    }
    if (found) outY = best;
    return found;
}

bool GroundGrid::HeightNear(float x, float z, float refY, float& outY) const {
    const std::vector<int>* cell = CellAt(x, z);
    if (!cell) return false;
    bool found = false;
    float best = 0, bestDist = FLT_MAX;
    for (int t : *cell) {
        float y;
        if (TriHeight(tris_[t * 3], tris_[t * 3 + 1], tris_[t * 3 + 2], x, z, y)) {
            float d = fabsf(y - refY);
            if (d < bestDist) { bestDist = d; best = y; found = true; }
        }
    }
    if (found) outY = best;
    return found;
}
