#include "pangya_gbin.h"
#include <ctype.h>

static const uint32_t VERSION_70 = 0x70;
static const uint32_t VERSION_71 = 0x71;
static const uint32_t VERSION_72 = 0x72;
static bool StartsWithBytes(const std::string& s, const unsigned char* b, size_t n) {
    if (s.size() < n) return false;
    return memcmp(s.data(), b, n) == 0;
}

SpecialPoint ClassifySpecialPoint(const std::string& name) {
    static const unsigned char TEE[]   = { 0xBD,0xC3,0xC0,0xDB,0xC1,0xA1 };    static const unsigned char PIN[]   = { 0xB3,0xA1,0xC1,0xA1 };    static const unsigned char GRID[]  = { 0xB1,0xD7,0xB8,0xAE,0xB5,0xE5 };    static const unsigned char SPAWN[] = { 0xB4,0xEB,0xC8,0xAD,0xB9,0xE6,0xBD,0xC3,0xC0,0xDB };    static const unsigned char SUN[]   = { 0xC5,0xC2,0xBE,0xE7 };    if (StartsWithBytes(name, SPAWN, sizeof(SPAWN))) return SpecialPoint::LobbySpawn;
    if (StartsWithBytes(name, TEE,   sizeof(TEE)))   return SpecialPoint::Tee;
    if (StartsWithBytes(name, PIN,   sizeof(PIN)))   return SpecialPoint::Pin;
    if (StartsWithBytes(name, GRID,  sizeof(GRID)))  return SpecialPoint::Grid;
    if (StartsWithBytes(name, SUN,   sizeof(SUN)))   return SpecialPoint::Sun;
    return SpecialPoint::None;
}

static bool IsAiBin(const char* path) {
    std::string s = path;
    for (auto& c : s) c = (char)tolower((unsigned char)c);
    return s.size() >= 6 && s.compare(s.size() - 6, 6, ".aibin") == 0;
}

static void LoadElement(Reader& r, uint32_t ver, GElement& e) {
    int opt = r.U8();
    r.U8();    r.Skip(2);    e.animFlag = opt & 7;
    e.collFlag = (opt >> 3) & 0x1F;
    e.faceNum = r.U32();
    e.name = r.FixedStr(32);
    e.minMax.Load(r);
    e.fitBase.Load(r);
    r.F32N(e.matrixWorld, 12);
    e.courseType = r.I32();
    if (ver > VERSION_70) e.script = r.FixedStr(32);
    if (ver > VERSION_71) {
        r.Skip(12 * 4);        r.Skip(8);    }
}

bool LoadGBin(const char* path, GBin& out) {
    out = GBin();
    Buf buf;
    if (!buf.Load(path)) { out.error = "arquivo nao encontrado"; return false; }
    Reader r(buf.data);

    char magic[5] = { 0 };
    r.Read(magic, 4);
    if (memcmp(magic, "WEPX", 4) != 0) { out.error = "nao e um .gbin (falta o magic WEPX)"; return false; }

    out.version = r.U32();
    if (out.version < VERSION_70 || out.version > VERSION_72) {
        out.error = "versao de .gbin desconhecida";
        return false;
    }
    const uint32_t ver = out.version;
    const bool aibin = IsAiBin(path);

    uint32_t nGlobal = r.U32();
    uint32_t nType0 = r.U32();
    uint32_t nType1 = r.U32();
    uint32_t nCamera = r.U32();
    uint32_t nLight = r.U32();
    uint32_t nSound = r.U32();
    uint32_t nTexture = r.U32();
    uint32_t nNode = r.U32();
    uint32_t nNewElement = (ver > VERSION_71) ? r.U32() : 0;

    const uint32_t LIMIT = 1000000;
    if (nGlobal > LIMIT || nType0 > LIMIT || nType1 > LIMIT || nCamera > LIMIT ||
        nLight > LIMIT || nSound > LIMIT || nTexture > LIMIT || nNode > LIMIT || nNewElement > LIMIT) {
        out.error = "cabecalho com contagens absurdas (arquivo corrompido?)";
        return false;
    }

    for (uint32_t i = 0; i < nCamera && r.ok; i++) {
        GCamera c;
        c.name = r.FixedStr(32);
        r.F32N(c.pos, 3);
        r.F32N(c.dest, 3);
        c.fov = r.F32();
        c.bank = r.F32();
        if (ver > VERSION_71) { r.Skip(6 * 4); r.Skip(8); }
        out.cameras.push_back(c);
    }

    for (uint32_t i = 0; i < nLight && r.ok; i++) {
        GLight l;
        l.type = r.U8();
        l.name = r.FixedStr(32);
        r.F32N(l.pos, 3);
        if (l.type != 0) l.data = r.FixedStr(64);
        if (ver > VERSION_71) { r.Skip(3 * 4); r.Skip(8); }
        l.special = ClassifySpecialPoint(l.name);
        out.lights.push_back(l);
    }

    for (uint32_t i = 0; i < nSound && r.ok; i++) {
        GSoundBox s;
        s.type = r.U8();
        s.name = r.FixedStr(64);
        s.box.Load(r);
        if (ver > VERSION_71) { AABBf tmp; tmp.Load(r); r.Skip(8); }
        out.soundBoxes.push_back(s);
    }

    for (uint32_t i = 0; i < nTexture && r.ok; i++)
        out.textures.push_back(r.FixedStr(32));

    for (uint32_t i = 0; i < nNode && r.ok; i++) {
        GNode n;
        n.name = r.FixedStr(aibin ? 32 : 16);
        uint32_t cnt = r.U32();
        n.type = (int)r.U32();
        if (cnt > LIMIT) { out.error = "node com pontos demais"; return false; }
        n.pts.resize(cnt * 3);
        for (uint32_t k = 0; k < cnt * 3; k++) n.pts[k] = r.F32();
        out.nodes.push_back(n);
    }

    const uint32_t nElements = nGlobal + nType0 + nType1;

    if (nElements > 0) {
        out.hasBase = true;
        LoadElement(r, ver, out.base);
        if (ver < VERSION_72) {
            if (out.base.faceNum > LIMIT) { out.error = "base com faces demais"; return false; }
            out.baseVtxColorV70.resize((size_t)out.base.faceNum * 3);
            for (size_t i = 0; i < out.baseVtxColorV70.size() && r.ok; i++)
                out.baseVtxColorV70[i] = r.U32();
        } else {
            uint32_t nMaps = r.U32();
            for (uint32_t i = 0; i < nMaps && r.ok; i++) {
                r.LenStr();
                uint32_t faceNum = r.U32();
                if (faceNum > LIMIT) { out.error = "map_color_vtx corrompido"; return false; }
                for (uint32_t j = 0; j < faceNum && r.ok; j++) {
                    uint32_t vtxNum = r.U32();
                    if (vtxNum > LIMIT) { out.error = "map_color_vtx corrompido"; return false; }
                    r.Skip((size_t)vtxNum * 4);
                }
            }
        }
    }

    out.elements.resize(nElements);
    for (uint32_t i = 0; i < nElements && r.ok; i++)
        LoadElement(r, ver, out.elements[i]);

    for (uint32_t i = 0; i < nNewElement && r.ok; i++) {
        GNewElement ne;
        r.Skip(8);        ne.name = r.FixedStr(64);
        r.F32N(ne.matrixWorld, 12);
        r.Skip(12 * 4);        ne.type = (int)r.U32();
        out.newElements.push_back(ne);
    }

    if (nElements > 0 && r.ok) {
        out.hasMapCheck = true;
        out.mapCheck.parHole = r.U8();
        if (ver < VERSION_72) {
            out.mapCheck.hasPoints = true;
            for (int i = 0; i < 2; i++) { out.mapCheck.tee[i][0] = r.F32(); out.mapCheck.tee[i][1] = r.F32(); }
            for (int i = 0; i < 2; i++)
                for (int j = 0; j < 3; j++) { out.mapCheck.pin[i][j][0] = r.F32(); out.mapCheck.pin[i][j][1] = r.F32(); }
        }
    }

    if (!r.ok) {
        out.error = "arquivo truncado (leitura passou do fim)";
    }
    out.valid = true;
    return true;
}
