#pragma once

#include <GL/glx.h>

#include "vmath.h"

struct Texture {
    GLuint gl_texture;
    GLenum target;
    Vector2 size;
};

int texture_init(struct Texture* texture, GLenum target, const Vector2* size);

int texture_read_from(struct Texture* texture, GLuint framebuffer, 
        GLenum buffer, const Vector2* pos, const Vector2* size);
