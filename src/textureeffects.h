#pragma once

#include "texture.h"
#include "framebuffer.h"

bool texture_blur(struct Framebuffer* buffer, struct Texture* texture, int stength);
