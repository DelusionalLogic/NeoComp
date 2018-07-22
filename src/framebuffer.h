#pragma once

#define GL_GLEXT_PROTOTYPES
#include <GL/glx.h>

#include "texture.h"
#include "renderbuffer.h"

#include <stdbool.h>

enum FramebufferTarget {
    FBT_BACKBUFFER           = 1 << 0,
    FBT_TEXTURE              = 1 << 1,
    FBT_RENDERBUFFER         = 1 << 2,
    FBT_RENDERBUFFER_STENCIL = 1 << 3,
};

struct Framebuffer {
    GLuint gl_fbo;
    enum FramebufferTarget target;

    // If we target a texture
    struct Texture* texture;

    // If we target a renderbuffer
    struct RenderBuffer* buffer;
    struct RenderBuffer* buffer_stencil;
};

bool framebuffer_init(struct Framebuffer* framebuffer);
bool framebuffer_initialized(struct Framebuffer* framebuffer);

void framebuffer_resetTarget(struct Framebuffer* framebuffer);
void framebuffer_targetTexture(struct Framebuffer* framebuffer, struct Texture* texture);
void framebuffer_targetRenderBuffer(struct Framebuffer* framebuffer, struct RenderBuffer* buffer);
void framebuffer_targetRenderBuffer_stencil(struct Framebuffer* framebuffer, struct RenderBuffer* buffer);
void framebuffer_targetBack(struct Framebuffer* framebuffer);

int framebuffer_bind(struct Framebuffer* framebuffer);
int framebuffer_rebind(struct Framebuffer* framebuffer);

int framebuffer_bind_read(struct Framebuffer* framebuffer);

void framebuffer_delete(struct Framebuffer* framebuffer);
