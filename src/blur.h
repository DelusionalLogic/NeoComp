#pragma once

#include "vmath.h"

#include "texture.h"
#include "framebuffer.h"
#include "renderbuffer.h"
#include "assets/face.h"

#include <GL/glx.h>

struct _win;

struct blur {
    struct Framebuffer fbo;
    GLuint array;
};

typedef struct {
    /// Framebuffer used for blurring.
    struct Framebuffer fbo;
    /// Textures used for blurring.
    struct Texture texture[2];
    struct RenderBuffer stencil;
    Vector2 size;
    /// Width of the textures.
    int width;
    /// Height of the textures.
    int height;
} glx_blur_cache_t;

#include "session.h"

void blur_init(struct blur* blur);
void blur_destroy(struct blur* blur);

bool blur_backbuffer(struct blur* blur, struct _session_t* ps, const Vector2* pos,
        const Vector2* size, float z, GLfloat factor_center,
        glx_blur_cache_t* pbc, struct _win* w);

bool blur_cache_init(glx_blur_cache_t* cache);
void blur_cache_delete(glx_blur_cache_t* cache);
bool blur_cache_resize(glx_blur_cache_t* cache, const Vector2* size);
