#pragma once
#include "raylib.h"
Texture2D LoadTextureDDS(const char* path);
Image LoadImageAny(const char* path);
Texture2D LoadTextureAny(const char* path);
void ApplyAlphaMask(Image* img, const char* maskPath);
