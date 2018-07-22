#pragma once

#include "vector.h"

#include "texture.h"
#include "framebuffer.h"

struct TextureBlurData {
    struct RenderBuffer* depth;
    struct Texture* tex;
    struct Texture* swap;
};

bool texture_blur(struct TextureBlurData* data, struct Framebuffer* buffer, int stength, bool transparent);
bool textures_blur(Vector* datas, struct Framebuffer* buffer, int stength, bool transparent);
