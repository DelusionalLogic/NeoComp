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

    // Per pools
    Vector to_blur;

    // Multiple use per frame
    Vector opaque_behind;
    Vector transparent_behind;
};

typedef struct glx_blur_cache {
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

void blursystem_init();
void blursystem_updateBlur(Swiss* em, Vector2* root_size,
        struct Texture* texture, int level, Vector* opaque, Vector* opaque_shadow, struct _session_t* ps);
void blursystem_delete(Swiss* em);
void blursystem_tick(Swiss* em, Vector* order);

bool blur_backbuffer(struct _session_t* ps, const Vector2* pos,
        const Vector2* size, float z, GLfloat factor_center,
        glx_blur_cache_t* pbc, struct _win* w);
