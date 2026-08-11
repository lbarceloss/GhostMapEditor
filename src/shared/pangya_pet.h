#pragma once
#include "pangya_io.h"
#include <vector>
#include <string>

struct PetTexture {
    std::string fn;    int flag = 0;
    int group = 0;
    uint32_t diffuse = 0xFFFFFFFF;
};

struct PetBone {
    std::string name;
    int parent = -1;
    float m[12] = { 1,0,0, 0,1,0, 0,0,1, 0,0,0 };};
struct PetVertex {
    float x = 0, y = 0, z = 0;
    int mainBone = 0;
    int   bone[4] = { 0, 0, 0, 0 };
    float weight[4] = { 1, 0, 0, 0 };
    int   nweights = 1;
};

struct PetCorner {
    uint32_t index = 0;    float nx = 0, ny = 0, nz = 0;
    float u = 0, v = 0;
};

struct PetPoly { PetCorner c[3]; };

struct PetVec3Key { float t = 0; float v[3] = { 0,0,0 }; };
struct PetQuatKey { float t = 0; float q[4] = { 0,0,0,1 }; };

struct PetBoneAnim {
    int boneId = -1;
    std::vector<PetVec3Key> pos, scl;
    std::vector<PetQuatKey> rot;
};

struct PetMotion {
    std::string name;    uint32_t frameStart = 0, frameEnd = 0;    std::string nextMove, rootBone;
};

struct Pet {
    int verMajor = 1, verMinor = 0;
    std::vector<PetTexture> textures;
    std::vector<PetBone>    bones;
    std::vector<PetVertex>  vertices;
    std::vector<PetPoly>    polys;
    std::vector<int>        texmap;    std::vector<PetBoneAnim> anims;    std::vector<PetMotion>   motions;
    bool valid = false;
    std::string error;
};
bool LoadPet(const char* path, Pet& out);
void PetBoneWorldMatrix(const Pet& pet, int boneIndex, float outM[12]);
void Mat4x3Apply(const float m[12], float x, float y, float z, float out[3]);
void Mat4x3Mul(const float a[12], const float b[12], float c[12]);
