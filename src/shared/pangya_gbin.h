#pragma once
#include "pangya_io.h"
#include <vector>
#include <string>
enum class SpecialPoint { None, Tee, Pin, Grid, LobbySpawn, Sun };
SpecialPoint ClassifySpecialPoint(const std::string& cp949name);

struct GCamera {
    std::string name;
    float pos[3] = { 0,0,0 };
    float dest[3] = { 0,0,0 };
    float fov = 0, bank = 0;
};

struct GLight {
    int type = 0;    std::string name;    float pos[3] = { 0,0,0 };
    std::string data;    SpecialPoint special = SpecialPoint::None;
};

struct GSoundBox {
    int type = 0;
    std::string name;
    AABBf box;
};

struct GNode {
    std::string name;    int type = 0;
    std::vector<float> pts;};

struct GElement {
    int animFlag = 0, collFlag = 0;
    uint32_t faceNum = 0;
    std::string name;    AABBf minMax, fitBase;
    float matrixWorld[12] = { 1,0,0, 0,1,0, 0,0,1, 0,0,0 };
    int courseType = 0;
    std::string script;
};

struct GNewElement {    std::string name;
    int type = 0;
    float matrixWorld[12] = { 1,0,0, 0,1,0, 0,0,1, 0,0,0 };
};

struct GMapCheck {
    int parHole = 0;
    float tee[2][2] = { {0,0},{0,0} };    float pin[2][3][2] = { 0 };    bool hasPoints = false;};

struct GBin {
    uint32_t version = 0x71;
    std::vector<GCamera>     cameras;
    std::vector<GLight>      lights;
    std::vector<GSoundBox>   soundBoxes;
    std::vector<std::string> textures;
    std::vector<GNode>       nodes;
    std::vector<GElement>    elements;
    std::vector<GNewElement> newElements;

    bool hasBase = false;
    GElement base;
    std::vector<uint32_t> baseVtxColorV70;

    bool hasMapCheck = false;
    GMapCheck mapCheck;

    bool valid = false;
    std::string error;
};

bool LoadGBin(const char* path, GBin& out);
