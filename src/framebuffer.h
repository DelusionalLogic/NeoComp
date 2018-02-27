#pragma once

#define GL_GLEXT_PROTOTYPES
#include <GL/glx.h>

#include "texture.h"

#include <stdbool.h>

enum FramebufferTarget {
    FBT_BACKBUFFER = 1 << 0,
    FBT_TEXTURE    = 1 << 1,
};

struct Framebuffer {
    GLuint gl_fbo;
    enum FramebufferTarget target;

    // If we target a texture
    struct Texture* texture;
};

bool framebuffer_init(struct Framebuffer* framebuffer);

void framebuffer_resetTarget(struct Framebuffer* framebuffer);
void framebuffer_targetTexture(struct Framebuffer* framebuffer, struct Texture* texture);
void framebuffer_targetBack(struct Framebuffer* framebuffer);

bool framebuffer_bind(struct Framebuffer* framebuffer);

void framebuffer_delete(struct Framebuffer* framebuffer);
