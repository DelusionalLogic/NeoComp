#include "shadow.h"

#include "assets/assets.h"
#include "assets/shader.h"
#include "shaders/shaderinfo.h"
#include "textureeffects.h"

#include "renderutil.h"

#include <assert.h>

static Vector2 X11_rectpos_to_gl(session_t *ps, const Vector2* xpos, const Vector2* size) {
    Vector2 glpos = {{
        xpos->x, ps->root_height - xpos->y - size->y
    }};
    return glpos;
}

int shadow_cache_init(struct glx_shadow_cache* cache, const Vector2* size) {
    Vector2 border = {{32, 32}};
    cache->border = border;
    cache->wSize = *size;

    Vector2 overflowSize = border;
    vec2_imul(&overflowSize, 2);
    vec2_add(&overflowSize, size);

    if(texture_init(&cache->texture, GL_TEXTURE_2D, &overflowSize) != 0) {
        printf("Couldn't create texture for shadow\n");
        return 1;
    }

    if(texture_init(&cache->effect, GL_TEXTURE_2D, &overflowSize) != 0) {
        printf("Couldn't create effect texture for shadow\n");
        texture_delete(&cache->texture);
        return 1;
    }

    if(renderbuffer_stencil_init(&cache->stencil, &overflowSize) != 0) {
        printf("Couldn't create renderbuffer stencil for shadow\n");
        texture_delete(&cache->texture);
        texture_delete(&cache->effect);
        return 1;
    }
    cache->initialized = true;
    return 0;
}

int shadow_cache_resize(struct glx_shadow_cache* cache, const Vector2* size) {
    assert(cache->initialized == true);
    Vector2 border = {{32, 32}};
    cache->wSize = *size;

    Vector2 overflowSize = border;
    vec2_imul(&overflowSize, 2);
    vec2_add(&overflowSize, size);

    texture_resize(&cache->texture, &overflowSize);
    texture_resize(&cache->effect, &overflowSize);

    renderbuffer_resize(&cache->stencil, &overflowSize);

    return 0;
}

void shadow_cache_delete(struct glx_shadow_cache* cache) {
    if(!cache->initialized)
        return;

    texture_delete(&cache->texture);
    texture_delete(&cache->effect);
    renderbuffer_delete(&cache->stencil);
    cache->initialized = false;
    return;
}

void win_calc_shadow(session_t* ps, win* w) {
    Vector2 size = {{w->widthb, w->heightb}};
    struct face* face = assets_load("window.face");

    // @BUG: If the size is 0 we will never initialize.
    if(!w->shadow_cache.initialized) {
        shadow_cache_init(&w->shadow_cache, &size);
    }

    shadow_cache_resize(&w->shadow_cache, &size);

    struct Framebuffer framebuffer;
    if(!framebuffer_init(&framebuffer)) {
        printf("Couldn't create framebuffer for shadow\n");
        return;
    }

    framebuffer_targetTexture(&framebuffer, &w->shadow_cache.texture);
    framebuffer_targetRenderBuffer_stencil(&framebuffer, &w->shadow_cache.stencil);
    framebuffer_bind(&framebuffer);
    Matrix old_view = view;
    view = mat4_orthogonal(0, w->shadow_cache.texture.size.x, 0, w->shadow_cache.texture.size.y, -1, 1);

    glViewport(0, 0, w->shadow_cache.texture.size.x, w->shadow_cache.texture.size.y);

    glEnable(GL_STENCIL_TEST);

    glClearColor(0.0, 0.0, 0.0, 0.0);

    glStencilMask(0xFF);
    glClearStencil(0);
    glStencilFunc(GL_EQUAL, 0, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_INCR);

    glClear(GL_STENCIL_BUFFER_BIT | GL_COLOR_BUFFER_BIT);

    // @CLEANUP: We have to do this since the window isn't using the new nice
    // interface
    /* glActiveTexture(GL_TEXTURE0); */
    /* glBindTexture(GL_TEXTURE_2D, w->paint.ptex->texture); */
    assert(w->drawable.bound);
    texture_bind(&w->drawable.texture, GL_TEXTURE0);

    struct shader_program* global_program = assets_load("shadow.shader");
    if(global_program->shader_type_info != &global_info) {
        printf_errf("Shader was not a global shader\n");
        framebuffer_delete(&framebuffer);
        view = old_view;
        return;
    }

    struct Global* global_type = global_program->shader_type;
    shader_use(global_program);

    shader_set_uniform_float(global_type->invert, false);
    shader_set_uniform_float(global_type->flip, true);
    shader_set_uniform_float(global_type->opacity, 1.0);
    shader_set_uniform_sampler(global_type->tex_scr, 0);

    {

#ifdef DEBUG_GLX
        printf_dbgf("SHADOW %f, %f, %f, %f\n", relpos.x, relpos.y, scale.x, scale.y);
#endif

        {
            Vector3 pos = vec3_from_vec2(&w->shadow_cache.border, 0.0);
            draw_rect(face, global_type->mvp, pos, size);
        }
    }

    glDisable(GL_STENCIL_TEST);
    view = old_view;

    // Do the blur
    struct TextureBlurData blurData = {
        .buffer = &framebuffer,
        .swap = &w->shadow_cache.effect,
    };
    if(!texture_blur(&blurData, &w->shadow_cache.texture, 3, true)) {
        printf_errf("Failed blurring the background texture");
        framebuffer_delete(&framebuffer);
        return;
    }

    framebuffer_resetTarget(&framebuffer);
    framebuffer_targetTexture(&framebuffer, &w->shadow_cache.effect);
    framebuffer_targetRenderBuffer_stencil(&framebuffer, &w->shadow_cache.stencil);
    if(framebuffer_bind(&framebuffer) != 0) {
        printf("Failed binding framebuffer to clip shadow\n");
        framebuffer_delete(&framebuffer);
        return;
    }
    old_view = view;
    view = mat4_orthogonal(0, w->shadow_cache.effect.size.x, 0, w->shadow_cache.effect.size.y, -1, 1);
    glViewport(0, 0, w->shadow_cache.effect.size.x, w->shadow_cache.effect.size.y);

    glClearColor(0.0, 0.0, 0.0, 0.0);
    glClear(GL_COLOR_BUFFER_BIT);

    glEnable(GL_STENCIL_TEST);

    glStencilMask(0xFF);
    glStencilFunc(GL_EQUAL, 0, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);

    draw_tex(ps, face, &w->shadow_cache.texture, &VEC3_ZERO, &w->shadow_cache.effect.size);

    glDisable(GL_STENCIL_TEST);
    view = old_view;

    framebuffer_delete(&framebuffer);
}

void win_paint_shadow(session_t* ps, win* w, const Vector2* pos, const Vector2* size, float z) {
    glx_mark(ps, w->id, true);
    struct glx_shadow_cache* cache = &w->shadow_cache;

    struct face* face = assets_load("window.face");

    glEnable(GL_BLEND);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    static const GLenum DRAWBUFS[2] = { GL_BACK_LEFT };
    glDrawBuffers(1, DRAWBUFS);

    glViewport(0, 0, ps->root_width, ps->root_height);

    /* { */
    /*     Vector3 rpos = {{0, 0, 1}}; */
    /*     Vector2 rsize = {{500, 500}}; */
    /*     draw_tex(ps, face, &cache->effect, &rpos, &rsize); */
    /* } */

    {
        Vector2 rpos = *pos;
        vec2_sub(&rpos, &cache->border);
        Vector3 tdrpos = vec3_from_vec2(&rpos, z);
        Vector2 rsize = cache->texture.size;

        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        draw_tex(ps, face, &cache->effect, &tdrpos, &rsize);
        glDepthMask(GL_TRUE);
        glDisable(GL_DEPTH_TEST);
    }

    glDisable(GL_BLEND);
}
