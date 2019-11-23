#pragma once

#include "swiss.h"
#include "vmath.h"
#include "vector.h"
#include "texture.h"
#include "framebuffer.h"

struct _session_t;
struct _win;

struct glx_shadow_cache {
    bool initialized;
    struct Texture texture;
    struct Texture effect;
    struct RenderBuffer stencil;
    Vector2 wSize;
    Vector2 border;
};

int shadow_cache_init(struct glx_shadow_cache* cache);
int shadow_cache_resize(struct glx_shadow_cache* cache, const Vector2* size);
void shadow_cache_delete(struct glx_shadow_cache* cache);

void shadowsystem_delete(Swiss *em);
void shadowsystem_tick(Swiss* em);
void shadowsystem_updateShadow(struct _session_t* ps, Vector* paints);
