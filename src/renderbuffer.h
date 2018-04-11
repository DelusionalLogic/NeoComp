#pragma once

#define GL_GLEXT_PROTOTYPES
#include <GL/glx.h>

#include "vmath.h"

enum BufferType {
    BUFFERTYPE_COLOR,
    BUFFERTYPE_STENCIL,
};

struct RenderBuffer {
    GLuint gl_buffer;
    GLenum gl_type;

    Vector2 size;
    bool hasSpace;

    enum BufferType type;
};

int renderbuffer_init(struct RenderBuffer* buffer, const Vector2* size);
int renderbuffer_stencil_init(struct RenderBuffer* buffer, const Vector2* size);
void renderbuffer_delete(struct RenderBuffer* buffer);

bool renderbuffer_initialized(struct RenderBuffer* buffer);
void renderbuffer_resize(struct RenderBuffer* buffer, const Vector2* size);

void renderbuffer_bind_to_framebuffer(struct RenderBuffer* buffer, GLenum attachment);
