#include "pangya_pet.h"
#include <algorithm>
#include <utility>
#include <ctype.h>
enum {
    FT_TEXT = 1, FT_SMTL = 2, FT_BONE = 4, FT_ANIM = 8, FT_MESH = 16,
    FT_FANM = 32, FT_FRAM = 64, FT_MOTI = 128, FT_EXTR = 256, FT_COLL = 512,
    FT_ALL = 1023, FT_SKINLIST = 65536
};
static const int FILE_PET  = FT_ALL;
static const int FILE_APET = FT_BONE | FT_ANIM | FT_FRAM | FT_MOTI;
static const int FILE_BPET = FT_BONE | FT_EXTR | FT_COLL;
static const int FILE_MPET = FT_TEXT | FT_SMTL | FT_BONE | FT_MESH | FT_FANM | FT_SKINLIST;

static bool EndsWith(const std::string& s, const char* suf) {
    size_t n = strlen(suf);
    return s.size() >= n && s.compare(s.size() - n, n, suf) == 0;
}

static int FileTypeFromPath(const char* path) {
    std::string s = path;
    for (auto& c : s) c = (char)tolower((unsigned char)c);
    if (EndsWith(s, ".mpet")) return FILE_MPET;
    if (EndsWith(s, ".apet")) return FILE_APET;
    if (EndsWith(s, ".bpet")) return FILE_BPET;
    return FILE_PET;
}
static int CmpVer(int aMaj, int aMin, int bMaj, int bMin) {
    if (aMaj != bMaj) return aMaj < bMaj ? -1 : 1;
    if (aMin != bMin) return aMin < bMin ? -1 : 1;
    return 0;
}

void Mat4x3Apply(const float m[12], float x, float y, float z, float out[3]) {
    out[0] = m[0] * x + m[3] * y + m[6] * z + m[9];
    out[1] = m[1] * x + m[4] * y + m[7] * z + m[10];
    out[2] = m[2] * x + m[5] * y + m[8] * z + m[11];
}

void Mat4x3Mul(const float a[12], const float b[12], float c[12]) {
    for (int col = 0; col < 3; col++) {
        float x = b[col * 3 + 0], y = b[col * 3 + 1], z = b[col * 3 + 2];
        c[col * 3 + 0] = a[0] * x + a[3] * y + a[6] * z;
        c[col * 3 + 1] = a[1] * x + a[4] * y + a[7] * z;
        c[col * 3 + 2] = a[2] * x + a[5] * y + a[8] * z;
    }
    float t[3];
    Mat4x3Apply(a, b[9], b[10], b[11], t);
    c[9] = t[0]; c[10] = t[1]; c[11] = t[2];
}

void PetBoneWorldMatrix(const Pet& pet, int boneIndex, float outM[12]) {
    static const float ident[12] = { 1,0,0, 0,1,0, 0,0,1, 0,0,0 };
    memcpy(outM, ident, sizeof(ident));
    if (boneIndex < 0 || boneIndex >= (int)pet.bones.size()) return;
    int chain[256];
    int n = 0;
    int b = boneIndex;
    while (b >= 0 && b < (int)pet.bones.size() && n < 256) {
        chain[n++] = b;
        int parent = pet.bones[b].parent;
        if (parent == b) break;        b = parent;
    }
    float acc[12];
    memcpy(acc, ident, sizeof(ident));
    for (int i = n - 1; i >= 0; i--) {
        float tmp[12];
        Mat4x3Mul(acc, pet.bones[chain[i]].m, tmp);
        memcpy(acc, tmp, sizeof(tmp));
    }
    memcpy(outM, acc, sizeof(acc));
}

bool LoadPet(const char* path, Pet& out) {
    out = Pet();
    Buf buf;
    if (!buf.Load(path)) { out.error = "arquivo nao encontrado"; return false; }
    if (buf.data.empty()) { out.error = "arquivo vazio"; return false; }

    const int fileType = FileTypeFromPath(path);
    Reader f(buf.data);

    while (f.Left(8)) {
        char id[5] = { 0 };
        f.Read(id, 4);
        uint32_t size = f.U32();
        if (!f.ok || !f.Left(size)) break;

        Reader b(f.p + f.pos, size);
        f.Skip(size);

        if (!memcmp(id, "VERS", 4)) {
            out.verMinor = b.U8();
            out.verMajor = b.U8();
            b.U16();        }
        else if (!memcmp(id, "TEXT", 4)) {
            uint32_t count = b.U32();
            if (count > 4096) { out.error = "TEXT com contagem absurda"; return false; }
            out.textures.reserve(count);
            for (uint32_t i = 0; i < count && b.ok; i++) {
                PetTexture t;
                t.fn = b.FixedStr(32);
                t.flag = (int8_t)b.U8();
                t.group = b.U8();
                b.Skip(2);                t.diffuse = b.U32();
                b.U32();                out.textures.push_back(t);
            }
        }
        else if (!memcmp(id, "BONE", 4)) {
            uint32_t count = b.U8();
            if (count == 0) count = b.U16();
            if (count > 8192) { out.error = "BONE com contagem absurda"; return false; }
            out.bones.reserve(count);
            for (uint32_t i = 0; i < count && b.ok; i++) {
                PetBone bo;
                bo.name = b.CStr();
                bo.parent = b.BoneId();
                if (fileType != FILE_APET && (fileType & FT_BONE)) {
                    b.F32N(bo.m, 12);
                    if (CmpVer(out.verMajor, out.verMinor, 1, 3) >= 0) b.F32();                }
                out.bones.push_back(bo);
            }
        }
        else if (!memcmp(id, "MESH", 4)) {
            if (fileType == FILE_MPET) {
                uint32_t nEx = b.U8();
                for (uint32_t i = 0; i < nEx; i++) b.Skip(1 + 3 + 4 * 4);
            }
            uint32_t nv = b.U32();
            if (nv > 4000000) { out.error = "MESH com vertices demais"; return false; }
            out.vertices.resize(nv);
            for (uint32_t i = 0; i < nv && b.ok; i++) {
                PetVertex& v = out.vertices[i];
                v.x = b.F32(); v.y = b.F32(); v.z = b.F32();
                if (fileType == FILE_MPET) b.F32();                int wsum = 0, nw = 0;
                v.nweights = 0;
                while (wsum < 0xFF && b.ok) {
                    int w = b.U8();
                    int id2 = b.BoneId();
                    if (id2 < 0) id2 = 0;
                    if (nw == 0) v.mainBone = id2;
                    if (v.nweights < 4) {
                        v.bone[v.nweights] = id2;
                        v.weight[v.nweights] = w / 255.0f;
                        v.nweights++;
                    }
                    wsum += w;
                    nw++;
                    if (nw > 64) { b.ok = false; break; }
                }
                if (nw < 2) b.Skip(2);                if (v.nweights == 0) { v.nweights = 1; v.bone[0] = 0; v.weight[0] = 1.0f; }
                else {
                    float s = 0;
                    for (int k = 0; k < v.nweights; k++) s += v.weight[k];
                    if (s > 0.0001f) for (int k = 0; k < v.nweights; k++) v.weight[k] /= s;
                }
            }
            if (!b.ok) { out.error = "MESH truncada nos vertices"; return false; }

            uint32_t np = b.U32();
            if (np > 4000000) { out.error = "MESH com poligonos demais"; return false; }
            out.polys.resize(np);
            const bool hasUvCount = CmpVer(out.verMajor, out.verMinor, 1, 2) >= 0;
            for (uint32_t i = 0; i < np && b.ok; i++) {
                for (int k = 0; k < 3; k++) {
                    PetCorner& c = out.polys[i].c[k];
                    c.index = b.U32();
                    c.nx = b.F32(); c.ny = b.F32(); c.nz = b.F32();
                    int nuv = 1;
                    if (hasUvCount) nuv = b.U8();
                    for (int u = 0; u < nuv; u++) {
                        float uu = b.F32(), vv = b.F32();
                        if (u == 0) { c.u = uu; c.v = vv; }
                    }
                }
            }
            if (!b.ok) { out.error = "MESH truncada nos poligonos"; return false; }

            out.texmap.resize(np);
            for (uint32_t i = 0; i < np; i++) out.texmap[i] = (int)b.I8();
        }
        else if (!memcmp(id, "ANIM", 4)) {
            const bool hasOrient = CmpVer(out.verMajor, out.verMinor, 1, 3) >= 0;
            while (b.ok) {
                int boneId = b.BoneId();
                if (boneId < 0) break;                PetBoneAnim a;
                a.boneId = boneId;
                uint32_t n = b.U32();
                if (n > 1000000) { b.ok = false; break; }
                a.pos.resize(n);
                for (uint32_t i = 0; i < n && b.ok; i++) { a.pos[i].t = b.F32(); b.F32N(a.pos[i].v, 3); }
                n = b.U32();
                if (n > 1000000) { b.ok = false; break; }
                a.rot.resize(n);
                for (uint32_t i = 0; i < n && b.ok; i++) { a.rot[i].t = b.F32(); b.F32N(a.rot[i].q, 4); }
                n = b.U32();
                if (n > 1000000) { b.ok = false; break; }
                a.scl.resize(n);
                for (uint32_t i = 0; i < n && b.ok; i++) { a.scl[i].t = b.F32(); b.F32N(a.scl[i].v, 3); }
                if (hasOrient) {
                    n = b.U32();
                    if (n > 1000000) { b.ok = false; break; }
                    b.Skip((size_t)n * 8);                }
                if (b.ok) out.anims.push_back(std::move(a));
            }
        }
        else if (!memcmp(id, "MOTI", 4)) {
            uint32_t count = b.U32();
            if (count > 100000) { out.error = "MOTI com contagem absurda"; return false; }
            out.motions.reserve(count);
            for (uint32_t i = 0; i < count && b.ok; i++) {
                PetMotion m;
                m.name = b.LenStr();
                m.frameStart = b.U32();
                m.frameEnd = b.U32();
                m.nextMove = b.LenStr();
                b.LenStr();                b.F32();                m.rootBone = b.LenStr();
                out.motions.push_back(std::move(m));
            }
        }
    }

    if (out.bones.empty()) {
        PetBone b;
        b.name = "root";
        out.bones.push_back(b);
    }
    const int nverts = (int)out.vertices.size();
    for (auto& p : out.polys)
        for (int k = 0; k < 3; k++)
            if ((int)p.c[k].index >= nverts) p.c[k].index = 0;
    for (auto& t : out.texmap)
        if (t < 0 || t >= (int)out.textures.size()) t = -1;
    const int nbones = (int)out.bones.size();
    for (auto& v : out.vertices) {
        if (v.mainBone < 0 || v.mainBone >= nbones) v.mainBone = 0;
        for (int k = 0; k < v.nweights; k++)
            if (v.bone[k] < 0 || v.bone[k] >= nbones) v.bone[k] = v.mainBone;
    }
    if (fileType == FILE_APET || fileType == FILE_BPET)
        out.valid = out.bones.size() > 1;
    else
        out.valid = !out.polys.empty();

    if (!out.valid && out.error.empty())
        out.error = (fileType == FILE_APET || fileType == FILE_BPET) ? "sem esqueleto" : "sem geometria";
    return out.valid;
}
