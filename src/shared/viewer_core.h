#pragma once
// ============================================================================
//  viewer_core - nucleo de visualizacao compartilhado entre o
//  Ghost Pangya SIM (sim_app) e o Ghost Map Editor (map_editor).
//
//  Extraido do main.cpp do SIM em 2026-08-08 SEM alteracao de comportamento:
//  o render do pink_01 foi comparado pixel a pixel antes e depois.
//  Mexer aqui afeta OS DOIS aplicativos.
// ============================================================================
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"

#include "pangya_gbin.h"
#include "pangya_pet.h"
#include "asset_db.h"
#include "ground.h"

#include <string>
#include <vector>
#include <unordered_map>

// ---------------------------------------------------------------- config ---
struct Config {
    unsigned int addrX = 0x00E47E30, addrY = 0x00E47E34, addrZ = 0x00E47E38;
    char  process[128] = "ProjectG.exe";
    float ballRadius = 6.0f;
    float ballOff[3] = { 0, 0, 0 };
    std::string gbin;
    std::vector<std::string> extraRoots;
    float moveSpeed = 220.0f;
    float fov = 70.0f;
    std::string avatar;
    std::vector<std::string> avatarParts;
    std::vector<std::string> clothesRoots;
    float charSpeed = 18.0f;
    float charScale = 1.6f;
};

std::string Trim(const std::string& s);
void LoadConfig(const char* path, Config& c);
void SaveConfig(const char* path, const Config& c);

// ------------------------------------------------------------ coordenadas ---
// PangYa e D3D canhoto; o render em GL precisa inverter Z.
static inline Vector3 PangyaToWorld(float x, float y, float z) { return Vector3{ x, y, -z }; }
static inline Vector3 WorldToPangya(Vector3 v) { return Vector3{ v.x, v.y, -v.z }; }
Matrix MatrixFromPangya(const float m[12]);

// ----------------------------------------------------------------- shader ---
struct ViewShader {
    Shader sh = { 0 };
    int locAlphaCut = -1, locCamPos = -1, locFogColor = -1, locFogRange = -1;
    void Load();
    void SetCut(float v)   { SetShaderValue(sh, locAlphaCut, &v, SHADER_UNIFORM_FLOAT); }
    void SetCam(Vector3 v) { SetShaderValue(sh, locCamPos, &v, SHADER_UNIFORM_VEC3); }
    void SetFog(float r, float g, float b, bool on) {
        float v[4] = { r, g, b, on ? 1.0f : 0.0f };
        SetShaderValue(sh, locFogColor, v, SHADER_UNIFORM_VEC4);
    }
    void SetFogRange(float a, float b) {
        float v[2] = { a, b };
        SetShaderValue(sh, locFogRange, v, SHADER_UNIFORM_VEC2);
    }
};

// ------------------------------------------------------------------ malha ---
struct SubMesh {
    Mesh mesh = { 0 };
    Texture2D tex = { 0 };
    bool blend = false;
    int  tris = 0;
};

struct PetModel {
    std::vector<SubMesh> subs;
    BoundingBox bbox = { { 0,0,0 }, { 0,0,0 } };
    float radius = 0;
    Vector3 center = { 0,0,0 };
    int tris = 0;
    bool ok = false;
    std::string error;
};

struct Instance {
    PetModel* model = nullptr;
    Matrix xf = { 0 };
    Vector3 worldCenter = { 0,0,0 };
    float worldRadius = 0;
    int courseType = 0;
    const char* name = nullptr;
};

struct TexCache {
    std::unordered_map<std::string, Texture2D> map;
    Texture2D white = { 0 };
    Texture2D notFound = { 0 };
    std::vector<std::string> missingNames;
    int missing = 0;

    void Init();
    Texture2D Get(const std::string& name, const AssetDB& db);
    void Unload();
};

void ComputeBaseVertexColors(const Pet& pet, const GBin& g, std::vector<Color>& out);
PetModel* BuildModel(const Pet& pet, const AssetDB& db, TexCache& tc,
                     const std::vector<Color>* cornerColors);
void UnloadModel(PetModel* m);

// ------------------------------------------------------------------ cena ----
struct Marker {
    Vector3 pos;
    Color color;
    const char* label;
    int priority;
};

struct Scene {
    GBin gbin;
    std::string gbinPath;
    std::string mapPrefix;
    AssetDB db;
    TexCache tex;
    std::unordered_map<std::string, PetModel*> models;
    std::vector<Instance> instances;
    std::vector<Marker> markers;
    std::vector<Vector3> teePoints, pinPoints;
    std::vector<std::string> missingModelNames;
    GroundGrid ground;
    int markersSnapped = 0;
    int missingModels = 0;
    int totalTris = 0;
    bool loaded = false;
    std::string status;
    bool  hasFog = false;
    float fogR = 0.75f, fogG = 0.85f, fogB = 1.0f;
    float fogNear = 400, fogFar = 4000;
    Texture2D skyFar = { 0 };
    Color skyUp = Color{ 140, 190, 240, 255 };
    Color skyDn = Color{ 90, 150, 90, 255 };
    Mesh skyCyl = { 0 }, skyCap = { 0 };
    bool hasSky = false;

    void Unload();
};

std::string MapPrefixFromPath(const std::string& gbinPath);
bool LoadScene(Scene& sc, const std::string& gbinPath,
               const std::vector<std::string>& extraRoots);

// ---------------------------------------------------------------- camera ----
struct FreeCam {
    Vector3 pos = { 0, 100, 0 };
    float yaw = 0, pitch = 0;
    float speed = 220.0f;
    float fov = 70.0f;

    Vector3 Forward() const {
        return Vector3{ cosf(pitch) * sinf(yaw), sinf(pitch), -cosf(pitch) * cosf(yaw) };
    }
    Vector3 Right() const { return Vector3{ cosf(yaw), 0, sinf(yaw) }; }
    void LookAt(Vector3 target);
    Camera3D ToCamera() const {
        Camera3D c = { 0 };
        c.position = pos;
        c.target = Vector3Add(pos, Forward());
        c.up = Vector3{ 0, 1, 0 };
        c.fovy = fov;
        c.projection = CAMERA_PERSPECTIVE;
        return c;
    }
};

void DrawSky(const Scene& sc, const FreeCam& cam, ViewShader& vs, Material& mat, float dist);
bool CourseVisible(int courseType, int filter);

// -------------------------------------------------------------------- ui ----
// Fonte capaz de desenhar os nomes coreanos (CP949 -> UTF-8).
struct UiFont {
    Font font = { 0 };
    bool ok = false;

    void Load(const std::vector<std::string>& samples);
    void Unload();
    void Draw(const char* text, int x, int y, int size, Color c) const {
        if (ok) DrawTextEx(font, text, Vector2{ (float)x, (float)y }, (float)size, 1.0f, c);
        else DrawText(text, x, y, size, c);
    }
    int Measure(const char* text, int size) const {
        if (ok) return (int)MeasureTextEx(font, text, (float)size, 1.0f).x;
        return MeasureText(text, size);
    }
};

bool UiButton(Rectangle r, const char* label, bool active, bool clicked, int fs = 15);

// Renderiza um .pet numa RenderTexture e devolve a miniatura ja virada.
Texture2D RenderPieceThumb(const std::string& path, const AssetDB& db, TexCache& tc,
                           RenderTexture2D fbo, Shader shader, Material& mat,
                           int alphaCutLoc, int fogLoc);
