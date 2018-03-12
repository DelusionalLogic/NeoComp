#pragma once

#include "texture.h"
#include "framebuffer.h"

struct TextureBlurData {
    struct Framebuffer* buffer;
    struct Texture* swap;
};

bool texture_blur(struct TextureBlurData* data, struct Texture* texture, int stength, bool transparent);
