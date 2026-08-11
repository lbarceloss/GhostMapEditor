#include "dds_loader.h"
#include "pangya_io.h"
#include "platform_win.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <string>

#pragma pack(push, 1)
struct DdsPixelFormat {
    uint32_t size, flags, fourCC, rgbBitCount, rMask, gMask, bMask, aMask;
};
struct DdsHeader {
    uint32_t size, flags, height, width, pitchOrLinearSize, depth, mipMapCount;
    uint32_t reserved1[11];
    DdsPixelFormat pf;
    uint32_t caps, caps2, caps3, caps4, reserved2;
};
#pragma pack(pop)

static const uint32_t DDPF_FOURCC = 0x4;
static const uint32_t DDPF_RGB    = 0x40;

static uint32_t FourCC(const char* s) {
    return (uint32_t)(unsigned char)s[0] | ((uint32_t)(unsigned char)s[1] << 8) |
           ((uint32_t)(unsigned char)s[2] << 16) | ((uint32_t)(unsigned char)s[3] << 24);
}

static size_t CompressedSize(int w, int h, int blockBytes) {
    int bw = (w + 3) / 4; if (bw < 1) bw = 1;
    int bh = (h + 3) / 4; if (bh < 1) bh = 1;
    return (size_t)bw * bh * blockBytes;
}

static Image LoadImageDDS(const char* path) {
    Image empty = { 0 };
    Buf buf;
    if (!buf.Load(path)) return empty;
    if (buf.data.size() < 4 + sizeof(DdsHeader)) return empty;
    if (memcmp(buf.data.data(), "DDS ", 4) != 0) return empty;

    DdsHeader h;
    memcpy(&h, buf.data.data() + 4, sizeof(h));
    if (h.size != 124) return empty;
    if (h.width == 0 || h.height == 0 || h.width > 16384 || h.height > 16384) return empty;

    const unsigned char* pixels = buf.data.data() + 4 + sizeof(DdsHeader);
    size_t available = buf.data.size() - (4 + sizeof(DdsHeader));

    int mips = (int)h.mipMapCount;
    if (mips < 1) mips = 1;

    Image img = { 0 };
    img.width = (int)h.width;
    img.height = (int)h.height;
    img.mipmaps = 1;
    img.format = 0;

    if (h.pf.flags & DDPF_FOURCC) {
        int blockBytes = 0;
        if (h.pf.fourCC == FourCC("DXT1")) {
            img.format = PIXELFORMAT_COMPRESSED_DXT1_RGBA;
            blockBytes = 8;
        } else if (h.pf.fourCC == FourCC("DXT3")) {
            img.format = PIXELFORMAT_COMPRESSED_DXT3_RGBA;
            blockBytes = 16;
        } else if (h.pf.fourCC == FourCC("DXT5")) {
            img.format = PIXELFORMAT_COMPRESSED_DXT5_RGBA;
            blockBytes = 16;
        } else {
            return empty;        }
        size_t total = 0;
        int usable = 0;
        int w = img.width, hh = img.height;
        for (int i = 0; i < mips; i++) {
            size_t sz = CompressedSize(w, hh, blockBytes);
            if (total + sz > available) break;
            total += sz;
            usable++;
            w = w > 1 ? w / 2 : 1;
            hh = hh > 1 ? hh / 2 : 1;
        }
        if (usable == 0) return empty;
        img.mipmaps = usable;
        img.data = MemAlloc((unsigned int)total);
        memcpy(img.data, pixels, total);
    }
    else if (h.pf.flags & DDPF_RGB) {
        const int bpp = (int)h.pf.rgbBitCount / 8;
        if (bpp != 3 && bpp != 4) return empty;
        size_t need = (size_t)img.width * img.height * bpp;
        if (need > available) return empty;

        unsigned char* out = (unsigned char*)MemAlloc((unsigned int)(img.width * img.height * 4));
        const bool bgr = (h.pf.rMask == 0x00FF0000);
        for (int i = 0; i < img.width * img.height; i++) {
            const unsigned char* s = pixels + (size_t)i * bpp;
            out[i * 4 + 0] = bgr ? s[2] : s[0];
            out[i * 4 + 1] = s[1];
            out[i * 4 + 2] = bgr ? s[0] : s[2];
            out[i * 4 + 3] = (bpp == 4) ? s[3] : 255;
        }
        img.data = out;
        img.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
        img.mipmaps = 1;
    }
    else return empty;

    return img;
}

Texture2D LoadTextureDDS(const char* path) {
    Image img = LoadImageDDS(path);
    if (!img.data) return Texture2D{ 0 };
    Texture2D tex = LoadTextureFromImage(img);
    MemFree(img.data);
    return tex;
}

Image LoadImageAny(const char* path) {
    std::string s = path;
    for (auto& c : s) c = (char)tolower((unsigned char)c);
    if (s.size() >= 4 && s.compare(s.size() - 4, 4, ".dds") == 0)
        return LoadImageDDS(path);

    Image img = LoadImage(path);
    if (img.data) return img;
    int w = 0, h = 0;
    unsigned char* px = nullptr;
    if (DecodeImageToRGBA(path, &w, &h, &px)) {
        Image out = { 0 };
        out.width = w;
        out.height = h;
        out.mipmaps = 1;
        out.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
        out.data = MemAlloc((unsigned int)((size_t)w * h * 4));
        memcpy(out.data, px, (size_t)w * h * 4);
        FreeDecodedImage(px);
        return out;
    }
    return Image{ 0 };
}

Texture2D LoadTextureAny(const char* path) {
    Image img = LoadImageAny(path);
    if (!img.data) return Texture2D{ 0 };
    Texture2D tex = LoadTextureFromImage(img);
    MemFree(img.data);
    return tex;
}

void ApplyAlphaMask(Image* img, const char* maskPath) {
    if (!img || !img->data || !maskPath || !*maskPath) return;
    if (img->format >= PIXELFORMAT_COMPRESSED_DXT1_RGB) return;

    Image mask = LoadImageAny(maskPath);
    if (!mask.data) return;

    ImageFormat(img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    if (mask.width != img->width || mask.height != img->height)
        ImageResize(&mask, img->width, img->height);

    Color* mp = LoadImageColors(mask);
    if (mp) {
        unsigned char* p = (unsigned char*)img->data;
        const int n = img->width * img->height;
        for (int i = 0; i < n; i++) p[i * 4 + 3] = mp[i].r;        UnloadImageColors(mp);
    }
    UnloadImage(mask);
}
