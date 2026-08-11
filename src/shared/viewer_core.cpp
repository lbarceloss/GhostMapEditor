// ============================================================================
//  viewer_core.cpp - implementacao do nucleo compartilhado.
//  Corpo movido VERBATIM do main.cpp do SIM (2026-08-08). Ver viewer_core.h.
// ============================================================================
#include "viewer_core.h"
#include "dds_loader.h"
#include "platform_win.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <algorithm>

// ---------------------------------------------------------------- config ---
std::string Trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

void LoadConfig(const char* path, Config& c) {
    FILE* f = fopen(path, "r");
    if (!f) return;
    char line[2048];
    bool first = true;
    while (fgets(line, sizeof(line), f)) {
        char* p = line;
        if (first) {
            first = false;
            if ((unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF)
                p += 3;
        }
        if (p[0] == '#' || p[0] == '\n' || p[0] == '\r' || p[0] == 0) continue;
        char* eq = strchr(p, '=');
        if (!eq) continue;
        std::string k = Trim(std::string(p, eq - p));
        std::string v = Trim(std::string(eq + 1));
        if      (k == "addr_x")      c.addrX = (unsigned)strtoul(v.c_str(), 0, 16);
        else if (k == "addr_y")      c.addrY = (unsigned)strtoul(v.c_str(), 0, 16);
        else if (k == "addr_z")      c.addrZ = (unsigned)strtoul(v.c_str(), 0, 16);
        else if (k == "process")     { strncpy(c.process, v.c_str(), 127); c.process[127] = 0; }
        else if (k == "ball_radius") c.ballRadius = (float)atof(v.c_str());
        else if (k == "ball_off_x")  c.ballOff[0] = (float)atof(v.c_str());
        else if (k == "ball_off_y")  c.ballOff[1] = (float)atof(v.c_str());
        else if (k == "ball_off_z")  c.ballOff[2] = (float)atof(v.c_str());
        else if (k == "gbin")        c.gbin = v;
        else if (k == "asset_root")  { if (!v.empty()) c.extraRoots.push_back(v); }
        else if (k == "move_speed")  c.moveSpeed = (float)atof(v.c_str());
        else if (k == "fov")         c.fov = (float)atof(v.c_str());
        else if (k == "avatar")      c.avatar = v;
        else if (k == "avatar_part")  { if (!v.empty()) c.avatarParts.push_back(v); }
        else if (k == "clothes_root") { if (!v.empty()) c.clothesRoots.push_back(v); }
        else if (k == "char_speed")  c.charSpeed = (float)atof(v.c_str());
        else if (k == "char_scale")  c.charScale = (float)atof(v.c_str());
    }
    fclose(f);
}

void SaveConfig(const char* path, const Config& c) {
    FILE* f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "# Ghost Pangya SIM - configuracao (editavel sem recompilar)\n");
    fprintf(f, "# desenvolvido por Ghost - www.hkfirewall.com\n");
    fprintf(f, "# ---- visualizador ----\n");
    fprintf(f, "gbin=%s\n", c.gbin.c_str());
    for (const auto& r : c.extraRoots) fprintf(f, "asset_root=%s\n", r.c_str());
    fprintf(f, "move_speed=%.1f\n", c.moveSpeed);
    fprintf(f, "fov=%.1f\n", c.fov);
    fprintf(f, "# ---- modo personagem (tecla C) ----\n");
    fprintf(f, "# pasta do avatar: precisa ter o <x>_def.apet, o <x>_def.bpet e as pecas _def.mpet\n");
    fprintf(f, "avatar=%s\n", c.avatar.c_str());
    fprintf(f, "# pastas extras com roupas (as suas novas). O provador (tecla X) mexe nisso.\n");
    for (const auto& r : c.clothesRoots) fprintf(f, "clothes_root=%s\n", r.c_str());
    fprintf(f, "# roupa vestida: nome do arquivo .mpet, ou \"-slot\" pra deixar o slot vazio.\n");
    for (const auto& p : c.avatarParts) fprintf(f, "avatar_part=%s\n", p.c_str());
    fprintf(f, "char_speed=%.1f\n", c.charSpeed);
    fprintf(f, "# ---- bola ao vivo lida do ProjectG.exe (tecla J) ----\n");
    fprintf(f, "process=%s\n", c.process);
    fprintf(f, "addr_x=%08X\naddr_y=%08X\naddr_z=%08X\n", c.addrX, c.addrY, c.addrZ);
    fprintf(f, "ball_radius=%.2f\n", c.ballRadius);
    fprintf(f, "# ajuste fino da bola, se precisar (coordenadas do PangYa)\n");
    fprintf(f, "ball_off_x=%.3f\nball_off_y=%.3f\nball_off_z=%.3f\n",
            c.ballOff[0], c.ballOff[1], c.ballOff[2]);
    fclose(f);
}

Matrix MatrixFromPangya(const float m[12]) {
    Matrix r = { 0 };
    r.m0 = m[0];  r.m4 = m[3];  r.m8  = m[6];  r.m12 = m[9];
    r.m1 = m[1];  r.m5 = m[4];  r.m9  = m[7];  r.m13 = m[10];
    r.m2 = -m[2]; r.m6 = -m[5]; r.m10 = -m[8]; r.m14 = -m[11];
    r.m3 = 0;     r.m7 = 0;     r.m11 = 0;     r.m15 = 1;
    return r;
}

// ----------------------------------------------------------------- shader ---
static const char* VS_CODE =
"#version 330\n"
"in vec3 vertexPosition;\n"
"in vec2 vertexTexCoord;\n"
"in vec4 vertexColor;\n"
"uniform mat4 mvp;\n"
"uniform mat4 matModel;\n"
"out vec2 fragTexCoord;\n"
"out vec4 fragColor;\n"
"out vec3 fragWorld;\n"
"void main(){\n"
"  fragTexCoord = vertexTexCoord;\n"
"  fragColor = vertexColor;\n"
"  fragWorld = (matModel*vec4(vertexPosition,1.0)).xyz;\n"
"  gl_Position = mvp*vec4(vertexPosition,1.0);\n"
"}\n";

static const char* FS_CODE =
"#version 330\n"
"in vec2 fragTexCoord;\n"
"in vec4 fragColor;\n"
"in vec3 fragWorld;\n"
"uniform sampler2D texture0;\n"
"uniform vec4 colDiffuse;\n"
"uniform float alphaCut;\n"
"uniform vec3 camPos;\n"
"uniform vec4 fogColor;\n""uniform vec2 fogRange;\n""out vec4 finalColor;\n"
"void main(){\n"
"  vec4 c = texture(texture0, fragTexCoord)*colDiffuse*fragColor;\n"
"  if (c.a < alphaCut) discard;\n"
"  if (fogColor.a > 0.5){\n"
"    float d = length(fragWorld - camPos);\n"
"    float f = clamp((d - fogRange.x)/max(fogRange.y - fogRange.x, 1.0), 0.0, 1.0);\n"
"    c.rgb = mix(c.rgb, fogColor.rgb, f);\n"
"  }\n"
"  finalColor = c;\n"
"}\n";

void ViewShader::Load() {
    sh = LoadShaderFromMemory(VS_CODE, FS_CODE);
    locAlphaCut = GetShaderLocation(sh, "alphaCut");
    locCamPos   = GetShaderLocation(sh, "camPos");
    locFogColor = GetShaderLocation(sh, "fogColor");
    locFogRange = GetShaderLocation(sh, "fogRange");
}

// -------------------------------------------------------------- texturas ---
void TexCache::Init() {
    Image im = GenImageColor(4, 4, WHITE);
    white = LoadTextureFromImage(im);
    UnloadImage(im);

    Image ck = GenImageChecked(64, 64, 8, 8, Color{ 190, 195, 205, 255 }, Color{ 225, 228, 236, 255 });
    notFound = LoadTextureFromImage(ck);
    UnloadImage(ck);
    SetTextureWrap(notFound, TEXTURE_WRAP_REPEAT);
}

Texture2D TexCache::Get(const std::string& name, const AssetDB& db) {
    if (name.empty()) return white;
    std::string key = ToLower(PathFileName(name));
    auto it = map.find(key);
    if (it != map.end()) return it->second;

    Texture2D t = { 0 };
    std::string path = db.FindTexture(name);
    if (!path.empty()) {
        Image img = LoadImageAny(path.c_str());
        if (img.data) {
            std::string maskPath = db.FindTexture(PathStem(path) + "_mask.png");
            if (!maskPath.empty()) ApplyAlphaMask(&img, maskPath.c_str());
            t = LoadTextureFromImage(img);
            MemFree(img.data);
        }
    }
    if (t.id == 0) { t = notFound; missing++; missingNames.push_back(name); }
    else {
        SetTextureWrap(t, TEXTURE_WRAP_REPEAT);
        SetTextureFilter(t, t.mipmaps > 1 ? TEXTURE_FILTER_TRILINEAR : TEXTURE_FILTER_BILINEAR);
    }
    map[key] = t;
    return t;
}

void TexCache::Unload() {
    for (auto& kv : map)
        if (kv.second.id != white.id && kv.second.id != notFound.id) UnloadTexture(kv.second);
    map.clear();
    if (white.id) UnloadTexture(white);
    if (notFound.id) UnloadTexture(notFound);
    white = Texture2D{ 0 };
    notFound = Texture2D{ 0 };
    missingNames.clear();
    missing = 0;
}

// ------------------------------------------------------------------ malha ---
void ComputeBaseVertexColors(const Pet& pet, const GBin& g, std::vector<Color>& out) {
    out.assign(pet.polys.size() * 3, WHITE);
    if (g.version >= 0x72) return;
    if (g.base.faceNum != pet.polys.size()) return;
    if (g.baseVtxColorV70.size() != pet.polys.size() * 3) return;
    if (pet.bones.empty()) return;

    const std::string& rootBone = pet.bones[0].name;
    std::vector<int> order;
    std::unordered_map<int, std::vector<int>> groups;
    for (size_t i = 0; i < pet.polys.size(); i++) {
        int bone = pet.vertices[pet.polys[i].c[0].index].mainBone;
        if (pet.bones[bone].name != rootBone) continue;
        int t = pet.texmap[i];
        if (groups.find(t) == groups.end()) order.push_back(t);
        groups[t].push_back((int)i);
    }
    size_t seq = 0;
    for (int gi = (int)order.size() - 1; gi >= 0; gi--) {
        for (int polyIdx : groups[order[gi]]) {
            for (int k = 0; k < 3; k++) {
                if (seq >= g.baseVtxColorV70.size()) return;
                uint32_t c = g.baseVtxColorV70[seq++];
                out[(size_t)polyIdx * 3 + k] = Color{
                    (unsigned char)((c >> 16) & 0xFF),
                    (unsigned char)((c >> 8) & 0xFF),
                    (unsigned char)(c & 0xFF),
                    255 };            }
        }
    }
}

PetModel* BuildModel(const Pet& pet, const AssetDB& db, TexCache& tc,
                     const std::vector<Color>* cornerColors) {
    PetModel* pm = new PetModel();
    std::vector<float> boneW(pet.bones.size() * 12);
    for (size_t i = 0; i < pet.bones.size(); i++)
        PetBoneWorldMatrix(pet, (int)i, &boneW[i * 12]);

    std::vector<Vector3> pos(pet.vertices.size());
    for (size_t i = 0; i < pet.vertices.size(); i++) {
        const PetVertex& v = pet.vertices[i];
        float o[3];
        Mat4x3Apply(&boneW[(size_t)v.mainBone * 12], v.x, v.y, v.z, o);
        pos[i] = Vector3{ o[0], o[1], o[2] };
    }
    std::unordered_map<int, std::vector<int>> groups;
    for (size_t i = 0; i < pet.polys.size(); i++) groups[pet.texmap[i]].push_back((int)i);

    Vector3 mn = { 1e30f, 1e30f, 1e30f }, mx = { -1e30f, -1e30f, -1e30f };

    for (auto& kv : groups) {
        const int matIdx = kv.first;
        const std::vector<int>& polys = kv.second;
        if (polys.empty()) continue;

        SubMesh sm;
        sm.tris = (int)polys.size();
        Mesh& m = sm.mesh;
        m.triangleCount = (int)polys.size();
        m.vertexCount = m.triangleCount * 3;
        m.vertices  = (float*)MemAlloc(m.vertexCount * 3 * sizeof(float));
        m.texcoords = (float*)MemAlloc(m.vertexCount * 2 * sizeof(float));
        m.normals   = (float*)MemAlloc(m.vertexCount * 3 * sizeof(float));
        m.colors    = (unsigned char*)MemAlloc(m.vertexCount * 4);

        int w = 0;
        for (int pi : polys) {
            const PetPoly& p = pet.polys[pi];
            for (int k = 0; k < 3; k++) {
                const PetCorner& c = p.c[k];
                Vector3 v = pos[c.index];
                m.vertices[w * 3 + 0] = v.x;
                m.vertices[w * 3 + 1] = v.y;
                m.vertices[w * 3 + 2] = v.z;
                m.texcoords[w * 2 + 0] = c.u;
                m.texcoords[w * 2 + 1] = c.v;
                m.normals[w * 3 + 0] = c.nx;
                m.normals[w * 3 + 1] = c.ny;
                m.normals[w * 3 + 2] = c.nz;
                Color col = WHITE;
                if (cornerColors && (size_t)pi * 3 + k < cornerColors->size())
                    col = (*cornerColors)[(size_t)pi * 3 + k];
                m.colors[w * 4 + 0] = col.r;
                m.colors[w * 4 + 1] = col.g;
                m.colors[w * 4 + 2] = col.b;
                m.colors[w * 4 + 3] = col.a;

                if (v.x < mn.x) mn.x = v.x; if (v.x > mx.x) mx.x = v.x;
                if (v.y < mn.y) mn.y = v.y; if (v.y > mx.y) mx.y = v.y;
                if (v.z < mn.z) mn.z = v.z; if (v.z > mx.z) mx.z = v.z;
                w++;
            }
        }
        UploadMesh(&m, false);

        std::string texName;
        if (matIdx >= 0 && matIdx < (int)pet.textures.size()) texName = pet.textures[matIdx].fn;
        sm.tex = tc.Get(texName, db);
        sm.blend = texName.substr(0, 5).find(']') != std::string::npos;

        pm->tris += sm.tris;
        pm->subs.push_back(sm);
    }

    if (mx.x < mn.x) { mn = Vector3{ 0,0,0 }; mx = Vector3{ 0,0,0 }; }
    pm->bbox.min = mn;
    pm->bbox.max = mx;
    pm->center = Vector3Scale(Vector3Add(mn, mx), 0.5f);
    pm->radius = Vector3Length(Vector3Subtract(mx, pm->center));
    pm->ok = !pm->subs.empty();
    return pm;
}

void UnloadModel(PetModel* m) {
    if (!m) return;
    for (auto& s : m->subs) UnloadMesh(s.mesh);
    delete m;
}

// ------------------------------------------------------------------ cena ----
void Scene::Unload() {
    for (auto& kv : models) UnloadModel(kv.second);
    models.clear();
    instances.clear();
    markers.clear();
    teePoints.clear();
    pinPoints.clear();
    missingModelNames.clear();
    ground.Clear();
    markersSnapped = 0;
    tex.Unload();
    db.Clear();
    if (skyFar.id) { UnloadTexture(skyFar); skyFar = Texture2D{ 0 }; }
    if (hasSky) { UnloadMesh(skyCyl); UnloadMesh(skyCap); skyCyl = Mesh{ 0 }; skyCap = Mesh{ 0 }; }
    hasSky = false;
    hasFog = false;
    loaded = false;
    totalTris = 0;
    missingModels = 0;
}

static Mesh MakeSkyCylinder(int slices, float halfHeight) {
    Mesh m = { 0 };
    m.triangleCount = slices * 2;
    m.vertexCount = m.triangleCount * 3;
    m.vertices  = (float*)MemAlloc(m.vertexCount * 3 * sizeof(float));
    m.texcoords = (float*)MemAlloc(m.vertexCount * 2 * sizeof(float));
    m.colors    = (unsigned char*)MemAlloc(m.vertexCount * 4);
    int w = 0;
    auto put = [&](float ang, float y, float u, float v) {
        m.vertices[w * 3 + 0] = sinf(ang);
        m.vertices[w * 3 + 1] = y;
        m.vertices[w * 3 + 2] = cosf(ang);
        m.texcoords[w * 2 + 0] = u;
        m.texcoords[w * 2 + 1] = v;
        m.colors[w * 4 + 0] = 255; m.colors[w * 4 + 1] = 255;
        m.colors[w * 4 + 2] = 255; m.colors[w * 4 + 3] = 255;
        w++;
    };
    for (int i = 0; i < slices; i++) {
        float a0 = (float)(2.0 * PI * i / slices);
        float a1 = (float)(2.0 * PI * (i + 1) / slices);
        float u0 = (float)i / slices, u1 = (float)(i + 1) / slices;
        put(a0, halfHeight, u0, 0); put(a0, -halfHeight, u0, 1); put(a1, -halfHeight, u1, 1);
        put(a0, halfHeight, u0, 0); put(a1, -halfHeight, u1, 1); put(a1, halfHeight, u1, 0);
    }
    UploadMesh(&m, false);
    return m;
}

static Mesh MakeSkyCap(int slices, float y) {
    Mesh m = { 0 };
    m.triangleCount = slices;
    m.vertexCount = slices * 3;
    m.vertices  = (float*)MemAlloc(m.vertexCount * 3 * sizeof(float));
    m.texcoords = (float*)MemAlloc(m.vertexCount * 2 * sizeof(float));
    m.colors    = (unsigned char*)MemAlloc(m.vertexCount * 4);
    int w = 0;
    auto put = [&](float x, float yy, float z) {
        m.vertices[w * 3 + 0] = x; m.vertices[w * 3 + 1] = yy; m.vertices[w * 3 + 2] = z;
        m.texcoords[w * 2 + 0] = 0.5f; m.texcoords[w * 2 + 1] = 0.5f;
        m.colors[w * 4 + 0] = 255; m.colors[w * 4 + 1] = 255;
        m.colors[w * 4 + 2] = 255; m.colors[w * 4 + 3] = 255;
        w++;
    };
    for (int i = 0; i < slices; i++) {
        float a0 = (float)(2.0 * PI * i / slices);
        float a1 = (float)(2.0 * PI * (i + 1) / slices);
        put(0, y, 0);
        put(sinf(a0), y, cosf(a0));
        put(sinf(a1), y, cosf(a1));
    }
    UploadMesh(&m, false);
    return m;
}

static Color AverageImageColor(const char* path) {
    Image im = LoadImageAny(path);
    Color c = Color{ 128, 160, 200, 255 };
    if (im.data) {
        Color* px = LoadImageColors(im);
        if (px) {
            long r = 0, g = 0, b = 0;
            int n = im.width * im.height;
            for (int i = 0; i < n; i++) { r += px[i].r; g += px[i].g; b += px[i].b; }
            if (n > 0) c = Color{ (unsigned char)(r / n), (unsigned char)(g / n), (unsigned char)(b / n), 255 };
            UnloadImageColors(px);
        }
        UnloadImage(im);
    }
    return c;
}

static void LoadFog(Scene& sc) {
    std::string p = sc.db.Find(sc.mapPrefix + "_fog.txt");
    if (p.empty()) return;
    FILE* f = fopen(p.c_str(), "r");
    if (!f) return;
    float r, g, b, n, fa;
    if (fscanf(f, "%f %f %f", &r, &g, &b) == 3 && fscanf(f, "%f %f", &n, &fa) == 2) {
        sc.fogR = r / 255.0f; sc.fogG = g / 255.0f; sc.fogB = b / 255.0f;
        sc.fogNear = n;
        sc.fogFar = (fa > n + 1.0f) ? fa : n + 1000.0f;
        sc.hasFog = true;
    }
    fclose(f);
}

static void LoadSky(Scene& sc) {
    std::string farp = sc.db.FindTexture(sc.mapPrefix + "_far.jpg");
    if (farp.empty()) return;
    sc.skyFar = LoadTextureAny(farp.c_str());
    if (sc.skyFar.id == 0) return;
    SetTextureWrap(sc.skyFar, TEXTURE_WRAP_REPEAT);
    SetTextureFilter(sc.skyFar, TEXTURE_FILTER_BILINEAR);

    std::string up = sc.db.FindTexture(sc.mapPrefix + "_up.jpg");
    std::string dn = sc.db.FindTexture(sc.mapPrefix + "_dn.jpg");
    if (!up.empty()) sc.skyUp = AverageImageColor(up.c_str());
    if (!dn.empty()) sc.skyDn = AverageImageColor(dn.c_str());

    sc.skyCyl = MakeSkyCylinder(64, 1.0f);
    sc.skyCap = MakeSkyCap(64, 1.0f);
    sc.hasSky = true;
}

std::string MapPrefixFromPath(const std::string& gbinPath) {
    std::string stem = PathStem(gbinPath);    size_t u = stem.find_last_of('_');
    return (u == std::string::npos) ? stem : stem.substr(0, u);
}

bool LoadScene(Scene& sc, const std::string& gbinPath, const std::vector<std::string>& extraRoots) {
    sc.Unload();
    sc.gbinPath = gbinPath;
    sc.mapPrefix = MapPrefixFromPath(gbinPath);

    if (!LoadGBin(gbinPath.c_str(), sc.gbin)) {
        sc.status = "ERRO: " + sc.gbin.error;
        return false;
    }
    sc.tex.Init();
    sc.db.AddRoot(PathDir(gbinPath));
    sc.db.AddRoot(AssetDB::GuessMapRoot(gbinPath));
    for (const auto& r : extraRoots) sc.db.AddRoot(r);
    auto loadPetModel = [&](const std::string& name, const std::vector<Color>* colors) -> PetModel* {
        std::string key = ToLower(name);
        if (!colors) {
            auto it = sc.models.find(key);
            if (it != sc.models.end()) return it->second;
        }
        std::string path = sc.db.Find(name);
        if (path.empty()) {
            sc.missingModels++;
            sc.missingModelNames.push_back(name);
            sc.models[key] = nullptr;
            return nullptr;
        }
        Pet pet;
        if (!LoadPet(path.c_str(), pet)) {
            TraceLog(LOG_WARNING, "PET falhou: %s (%s)", name.c_str(), pet.error.c_str());
            sc.missingModels++;
            sc.missingModelNames.push_back(name + "  (" + pet.error + ")");
            sc.models[key] = nullptr;
            return nullptr;
        }
        PetModel* pm = BuildModel(pet, sc.db, sc.tex, colors);
        sc.models[key] = pm;
        return pm;
    };

    if (sc.gbin.hasBase) {
        std::string path = sc.db.Find(sc.gbin.base.name);
        std::vector<Color> colors;
        PetModel* pm = nullptr;
        if (!path.empty()) {
            Pet pet;
            if (LoadPet(path.c_str(), pet)) {
                ComputeBaseVertexColors(pet, sc.gbin, colors);
                pm = BuildModel(pet, sc.db, sc.tex, &colors);
                sc.models[ToLower(sc.gbin.base.name) + "#base"] = pm;
            }
        }
        if (!pm) { sc.missingModels++; sc.missingModelNames.push_back(sc.gbin.base.name + "  (terreno)"); }
        else {
            Instance in;
            in.model = pm;
            in.xf = MatrixFromPangya(sc.gbin.base.matrixWorld);
            in.worldCenter = Vector3Transform(pm->center, in.xf);
            in.worldRadius = pm->radius;
            in.courseType = sc.gbin.base.courseType;
            in.name = sc.gbin.base.name.c_str();
            sc.instances.push_back(in);
            sc.totalTris += pm->tris;
            for (const auto& s : pm->subs) sc.ground.AddMesh(s.mesh, in.xf);
            sc.ground.Finish();
        }
    }
    for (const auto& e : sc.gbin.elements) {
        PetModel* pm = loadPetModel(e.name, nullptr);
        if (!pm || !pm->ok) continue;
        Instance in;
        in.model = pm;
        in.xf = MatrixFromPangya(e.matrixWorld);
        in.worldCenter = Vector3Transform(pm->center, in.xf);
        in.worldRadius = pm->radius * 1.05f;
        in.courseType = e.courseType;
        in.name = e.name.c_str();
        sc.instances.push_back(in);
        sc.totalTris += pm->tris;
    }
    for (const auto& l : sc.gbin.lights) {
        Vector3 p = PangyaToWorld(l.pos[0], l.pos[1], l.pos[2]);
        switch (l.special) {
            case SpecialPoint::Tee:
                sc.markers.push_back({ p, Color{ 60, 220, 90, 255 }, "TEE", 0 });
                sc.teePoints.push_back(p);
                break;
            case SpecialPoint::Pin:
                sc.markers.push_back({ p, Color{ 255, 70, 70, 255 }, "PIN", 1 });
                sc.pinPoints.push_back(p);
                break;
            case SpecialPoint::Grid:
                sc.markers.push_back({ p, Color{ 255, 210, 60, 255 }, "GRID", 2 });
                break;
            case SpecialPoint::LobbySpawn:
                sc.markers.push_back({ p, Color{ 120, 180, 255, 255 }, "LOBBY", 3 });
                break;
            default: break;
        }
    }
    for (const auto& ne : sc.gbin.newElements) {
        Vector3 p = PangyaToWorld(ne.matrixWorld[9], ne.matrixWorld[10], ne.matrixWorld[11]);
        if (ne.type == 1) { sc.markers.push_back({ p, Color{ 255, 70, 70, 255 }, "GREEN", 1 }); sc.pinPoints.push_back(p); }
        else if (ne.type == 2) { sc.markers.push_back({ p, Color{ 60, 220, 90, 255 }, "TEE", 0 }); sc.teePoints.push_back(p); }
    }
    auto snapToGround = [&](Vector3& p) {
        float y;
        if (!sc.ground.HeightAt(p.x, p.z, y)) return false;
        p.y = y;
        return true;
    };
    for (auto& mk : sc.markers) if (snapToGround(mk.pos)) sc.markersSnapped++;
    for (auto& p : sc.teePoints) snapToGround(p);
    for (auto& p : sc.pinPoints) snapToGround(p);
    std::stable_sort(sc.markers.begin(), sc.markers.end(),
                     [](const Marker& a, const Marker& b) { return a.priority < b.priority; });

    LoadFog(sc);
    LoadSky(sc);

    sc.loaded = true;
    char buf[512];
    snprintf(buf, sizeof(buf), "%s  |  %d objetos, %d triangulos, %d texturas  (%d faltando)",
             PathFileName(gbinPath).c_str(), (int)sc.instances.size(), sc.totalTris,
             (int)sc.tex.map.size(), sc.missingModels + sc.tex.missing);
    sc.status = buf;
    return true;
}

// ---------------------------------------------------------------- camera ----
void FreeCam::LookAt(Vector3 target) {
    Vector3 d = Vector3Subtract(target, pos);
    float len = Vector3Length(d);
    if (len < 0.001f) return;
    d = Vector3Scale(d, 1.0f / len);
    pitch = asinf(d.y);
    yaw = atan2f(d.x, -d.z);
}

void DrawSky(const Scene& sc, const FreeCam& cam, ViewShader& vs, Material& mat, float dist) {
    if (!sc.hasSky) return;
    rlDisableDepthMask();
    rlDisableDepthTest();
    vs.SetFog(0, 0, 0, false);
    vs.SetCut(0.0f);

    Matrix scale = MatrixScale(dist, dist * 0.9f, dist);
    Matrix trans = MatrixTranslate(cam.pos.x, cam.pos.y, cam.pos.z);
    Matrix xf = MatrixMultiply(scale, trans);

    mat.maps[MATERIAL_MAP_DIFFUSE].texture = sc.skyFar;
    mat.maps[MATERIAL_MAP_DIFFUSE].color = WHITE;
    DrawMesh(sc.skyCyl, mat, xf);
    mat.maps[MATERIAL_MAP_DIFFUSE].texture = sc.tex.white;
    mat.maps[MATERIAL_MAP_DIFFUSE].color = sc.skyUp;
    DrawMesh(sc.skyCap, mat, xf);
    mat.maps[MATERIAL_MAP_DIFFUSE].color = sc.skyDn;
    DrawMesh(sc.skyCap, mat, MatrixMultiply(MatrixScale(1, -1, 1), xf));

    mat.maps[MATERIAL_MAP_DIFFUSE].color = WHITE;
    rlEnableDepthTest();
    rlEnableDepthMask();
}

bool CourseVisible(int courseType, int filter) {
    if (filter == 0) return true;    return courseType == 0 || courseType == filter;
}

// -------------------------------------------------------------------- ui ----
void UiFont::Load(const std::vector<std::string>& samples) {
    Unload();
    std::string path = FindKoreanFont();
    if (path.empty()) return;

    std::string all;
    for (int c = 32; c < 127; c++) all.push_back((char)c);
    for (const std::string& s : samples) all += s;

    int total = 0;
    int* cps = LoadCodepoints(all.c_str(), &total);
    if (!cps || total <= 0) return;
    std::vector<int> uniq;
    uniq.reserve(total);
    for (int i = 0; i < total; i++) {
        bool dup = false;
        for (int u : uniq) if (u == cps[i]) { dup = true; break; }
        if (!dup && cps[i] > 0) uniq.push_back(cps[i]);
    }
    UnloadCodepoints(cps);
    if (uniq.empty()) return;

    font = LoadFontEx(path.c_str(), 20, uniq.data(), (int)uniq.size());
    ok = (font.texture.id != 0 && font.glyphCount > 0);
    if (ok) SetTextureFilter(font.texture, TEXTURE_FILTER_BILINEAR);
}

void UiFont::Unload() {
    if (ok) UnloadFont(font);
    font = Font{ 0 };
    ok = false;
}

bool UiButton(Rectangle r, const char* label, bool active, bool clicked, int fs) {
    const Vector2 m = GetMousePosition();
    const bool hover = CheckCollisionPointRec(m, r);
    Color bg = active ? Color{ 40, 110, 70, 235 } : Color{ 34, 40, 52, 225 };
    if (hover) bg = active ? Color{ 55, 150, 95, 245 } : Color{ 54, 64, 82, 245 };
    DrawRectangleRec(r, bg);
    DrawRectangleLinesEx(r, 1, hover ? Color{ 140, 200, 255, 255 } : Color{ 70, 84, 104, 255 });
    int tw = MeasureText(label, fs);
    if (tw > r.width - 8) {        static char buf[256];
        snprintf(buf, sizeof(buf), "%s", label);
        int n = (int)strlen(buf);
        while (n > 3 && MeasureText(buf, fs) > r.width - 8) buf[--n] = 0;
        label = buf;
        tw = MeasureText(label, fs);
    }
    DrawText(label, (int)(r.x + (r.width - tw) / 2), (int)(r.y + (r.height - fs) / 2), fs,
             active ? RAYWHITE : Color{ 200, 210, 225, 255 });
    return clicked && hover;
}

Texture2D RenderPieceThumb(const std::string& path, const AssetDB& db, TexCache& tc,
                           RenderTexture2D fbo, Shader shader, Material& mat,
                           int alphaCutLoc, int fogLoc) {
    Texture2D empty = { 0 };
    Pet pet;
    if (!LoadPet(path.c_str(), pet)) return empty;
    PetModel* pm = BuildModel(pet, db, tc, nullptr);
    if (!pm || !pm->ok) { UnloadModel(pm); return empty; }
    Matrix xf = MatrixMultiply(MatrixScale(1, 1, -1), MatrixRotateY(PI));
    Vector3 c = Vector3Transform(pm->center, xf);
    const float r = fmaxf(pm->radius, 0.001f);
    const float dist = r / tanf(35.0f * DEG2RAD) * 1.12f;

    Camera3D cam = { 0 };
    cam.position = Vector3{ c.x, c.y + r * 0.10f, c.z - dist };    cam.target = c;
    cam.up = Vector3{ 0, 1, 0 };
    cam.fovy = 70.0f;
    cam.projection = CAMERA_PERSPECTIVE;

    const float cut = 0.5f;
    const float fogOff[4] = { 0, 0, 0, 0 };
    SetShaderValue(shader, alphaCutLoc, &cut, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, fogLoc, fogOff, SHADER_UNIFORM_VEC4);

    BeginTextureMode(fbo);
    ClearBackground(Color{ 22, 26, 34, 255 });
    BeginMode3D(cam);
    rlDisableBackfaceCulling();
    for (const auto& s : pm->subs) {
        mat.maps[MATERIAL_MAP_DIFFUSE].texture = s.tex;
        DrawMesh(s.mesh, mat, xf);
    }
    rlEnableBackfaceCulling();
    EndMode3D();
    EndTextureMode();

    UnloadModel(pm);

    Image img = LoadImageFromTexture(fbo.texture);
    ImageFlipVertical(&img);    Texture2D t = LoadTextureFromImage(img);
    UnloadImage(img);
    if (t.id) SetTextureFilter(t, TEXTURE_FILTER_BILINEAR);
    return t;
}
