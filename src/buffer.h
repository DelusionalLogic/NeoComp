#pragma once

#include <stdbool.h>

#define GL_GLEXT_PROTOTYPES
#include <GL/glx.h>

struct BufferObject {
    GLuint gl;
    GLenum target;

    size_t size;
    bool allocated;
};

int bo_init(struct BufferObject* bo, const size_t size);
void bo_delete(struct BufferObject* bo);
bool bo_initialized(const struct BufferObject* bo);
void bo_update(struct BufferObject* bo, size_t offset, size_t size, void* data);
