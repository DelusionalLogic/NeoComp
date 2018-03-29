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

void window_shadow(session_t* ps, win* w, const Vector2* pos, const Vector2* size) {
    glx_mark(ps, w->id, true);
    struct glx_shadow_cache* cache = &w->shadow_cache;

    struct face* face = assets_load("window.face");
    bool hadScissor = glIsEnabled(GL_SCISSOR_TEST);

    glEnable(GL_BLEND);

    if(!vec2_eq(size, &cache->wSize) || true) {
        // @BUG: If the size is 0 we will never initialize.
        if(!cache->initialized) {
            shadow_cache_init(cache, size);
        }

        shadow_cache_resize(cache, size);

        struct Framebuffer framebuffer;
        if(!framebuffer_init(&framebuffer)) {
            printf("Couldn't create framebuffer for shadow\n");
            return;
        }

        framebuffer_targetTexture(&framebuffer, &cache->texture);
        framebuffer_targetRenderBuffer_stencil(&framebuffer, &cache->stencil);
        framebuffer_bind(&framebuffer);

        glViewport(0, 0, cache->texture.size.x, cache->texture.size.y);

        glEnable(GL_STENCIL_TEST);
        glDisable(GL_SCISSOR_TEST);

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
            return;
        }

        struct Global* global_type = global_program->shader_type;
        shader_use(global_program);

        shader_set_uniform_float(global_type->invert, false);
        shader_set_uniform_float(global_type->flip, true);
        shader_set_uniform_float(global_type->opacity, 1.0);
        shader_set_uniform_sampler(global_type->tex_scr, 0);

        {
            Vector2 pixeluv = {{1.0f, 1.0f}};
            vec2_div(&pixeluv, &cache->texture.size);

            Vector2 scale = pixeluv;
            vec2_mul(&scale, size);

            Vector2 relpos = pixeluv;
            vec2_mul(&relpos, &cache->border);

#ifdef DEBUG_GLX
            printf_dbgf("SHADOW %f, %f, %f, %f\n", relpos.x, relpos.y, scale.x, scale.y);
#endif

            draw_rect(face, global_type->mvp, relpos, scale);
        }

        glDisable(GL_STENCIL_TEST);

        // Do the blur
        struct TextureBlurData blurData = {
            .buffer = &framebuffer,
            .swap = &cache->effect,
        };
        if(!texture_blur(&blurData, &cache->texture, 3, true)) {
            printf_errf("Failed blurring the background texture");
            framebuffer_delete(&framebuffer);
            if(hadScissor)
                glEnable(GL_SCISSOR_TEST);
            return;
        }

        framebuffer_resetTarget(&framebuffer);
        framebuffer_targetTexture(&framebuffer, &cache->effect);
        framebuffer_targetRenderBuffer_stencil(&framebuffer, &cache->stencil);
        if(framebuffer_bind(&framebuffer) != 0) {
            printf("Failed binding framebuffer to clip shadow\n");
            framebuffer_delete(&framebuffer);
            if(hadScissor)
                glEnable(GL_SCISSOR_TEST);
            return;
        }
        glViewport(0, 0, cache->effect.size.x, cache->effect.size.y);

        glClearColor(0.0, 0.0, 0.0, 0.0);
        glClear(GL_COLOR_BUFFER_BIT);

        glEnable(GL_STENCIL_TEST);

        glStencilMask(0xFF);
        glStencilFunc(GL_EQUAL, 0, 0xFF);
        glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);

        draw_tex(ps, face, &cache->texture, &VEC2_ZERO, &VEC2_UNIT);

        glDisable(GL_STENCIL_TEST);

        framebuffer_delete(&framebuffer);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    static const GLenum DRAWBUFS[2] = { GL_BACK_LEFT };
    glDrawBuffers(1, DRAWBUFS);

    glViewport(0, 0, ps->root_width, ps->root_height);

    struct shader_program* global_program = assets_load("passthough.shader");
    if(global_program->shader_type_info != &passthough_info) {
        printf_errf("Shader was not a passthough shader\n");
        return;
    }

    /* { */
    /*     Vector2 rpos = {{0, 0}}; */
    /*     Vector2 rsize = {{.4, .6}}; */
    /*     draw_tex(ps, face, &cache->effect, &rpos, &rsize); */
    /* } */

    if(hadScissor)
        glEnable(GL_SCISSOR_TEST);

    Vector2 root_size = {{ps->root_width, ps->root_height}};
    {
        Vector2 rpos = X11_rectpos_to_gl(ps, pos, size);
        vec2_sub(&rpos, &cache->border);
        Vector2 rsize = cache->texture.size;

        Vector2 pixeluv = {{1.0f, 1.0f}};
        vec2_div(&pixeluv, &root_size);

        Vector2 scale = pixeluv;
        vec2_mul(&scale, &rsize);

        Vector2 relpos = pixeluv;
        vec2_mul(&relpos, &rpos);

        draw_tex(ps, face, &cache->effect, &relpos, &scale);
    }

    glDisable(GL_BLEND);
}
