#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

struct Buf {
    std::vector<unsigned char> data;
    bool Load(const char* path) {
        FILE* f = fopen(path, "rb");
        if (!f) return false;
        fseek(f, 0, SEEK_END);
        long n = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (n < 0) { fclose(f); return false; }
        data.resize((size_t)n);
        size_t rd = n > 0 ? fread(data.data(), 1, (size_t)n, f) : 0;
        fclose(f);
        data.resize(rd);
        return true;
    }
};
struct Reader {
    const unsigned char* p = nullptr;
    size_t size = 0, pos = 0;
    bool ok = true;

    Reader() {}
    Reader(const unsigned char* d, size_t n) : p(d), size(n) {}
    Reader(const std::vector<unsigned char>& v) : p(v.data()), size(v.size()) {}

    bool Left(size_t n) const { return pos + n <= size; }
    void Skip(size_t n) { if (!Left(n)) { ok = false; pos = size; } else pos += n; }

    void Read(void* dst, size_t n) {
        if (!Left(n)) { ok = false; memset(dst, 0, n); pos = size; return; }
        memcpy(dst, p + pos, n);
        pos += n;
    }
    uint8_t  U8()  { uint8_t  v = 0; Read(&v, 1); return v; }
    uint16_t U16() { uint16_t v = 0; Read(&v, 2); return v; }
    uint32_t U32() { uint32_t v = 0; Read(&v, 4); return v; }
    int16_t  I16() { int16_t  v = 0; Read(&v, 2); return v; }
    int32_t  I32() { int32_t  v = 0; Read(&v, 4); return v; }
    int8_t   I8()  { int8_t   v = 0; Read(&v, 1); return v; }
    float    F32() { float    v = 0; Read(&v, 4); return v; }
    void     F32N(float* dst, int n) { for (int i = 0; i < n; i++) dst[i] = F32(); }
    std::string FixedStr(size_t n) {
        if (!Left(n)) { ok = false; pos = size; return std::string(); }
        const char* s = (const char*)(p + pos);
        size_t len = 0;
        while (len < n && s[len] != 0) len++;
        std::string out(s, len);
        pos += n;
        return out;
    }
    std::string LenStr() {
        uint32_t n = U32();
        if (n == 0 || n > 1u << 20) { if (n > 1u << 20) ok = false; return std::string(); }
        if (!Left(n)) { ok = false; pos = size; return std::string(); }
        const char* s = (const char*)(p + pos);
        size_t len = 0;
        while (len < n && s[len] != 0) len++;
        std::string out(s, len);
        pos += n;
        return out;
    }
    std::string CStr() {
        std::string out;
        while (Left(1)) {
            char c = (char)p[pos++];
            if (c == 0) return out;
            out.push_back(c);
        }
        ok = false;
        return out;
    }
    int BoneId() {
        int id = U8();
        if (id == 0xFF) return -1;
        if (id == 0xFE) { id = (int)I16(); }
        if (id == 0xFFFF) return -1;
        return id;
    }
};

struct AABBf {
    float minx = 0, miny = 0, minz = 0, maxx = 0, maxy = 0, maxz = 0;
    void Load(Reader& r) {
        minx = r.F32(); miny = r.F32(); minz = r.F32();
        maxx = r.F32(); maxy = r.F32(); maxz = r.F32();
    }
};
