#include "shadow.h"

#include "assets/assets.h"
#include "assets/shader.h"
#include "shaders/shaderinfo.h"
#include "textureeffects.h"

#include "renderutil.h"

#include <assert.h>

int shadow_cache_init(struct glx_shadow_cache* cache) {
    Vector2 border = {{32, 32}};
    cache->border = border;

    if(texture_init(&cache->texture, GL_TEXTURE_2D, NULL) != 0) {
        printf("Couldn't create texture for shadow\n");
        return 1;
    }

    if(texture_init(&cache->effect, GL_TEXTURE_2D, NULL) != 0) {
        printf("Couldn't create effect texture for shadow\n");
        texture_delete(&cache->texture);
        return 1;
    }

    if(renderbuffer_stencil_init(&cache->stencil, NULL) != 0) {
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
}

void win_paint_shadow(session_t* ps, win* w, const Vector2* pos, const Vector2* size, float z) {
    glx_mark(ps, w->id, true);
    struct glx_shadow_cache* cache = &w->shadow_cache;

    glEnable(GL_BLEND);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    static const GLenum DRAWBUFS[2] = { GL_BACK_LEFT };
    glDrawBuffers(1, DRAWBUFS);

    glViewport(0, 0, ps->root_width, ps->root_height);

    /* { */
    /*     Vector3 rpos = {{0, 0, 1}}; */
    /*     Vector2 rsize = {{500, 500}}; */
    /*     draw_tex(face, &cache->effect, &rpos, &rsize); */
    /* } */

    struct shader_program* passthough_program = assets_load("passthough.shader");
    if(passthough_program->shader_type_info != &passthough_info) {
        printf_errf("Shader was not a passthough shader\n");
        return;
    }
    struct Passthough* passthough_type = passthough_program->shader_type;

    shader_set_future_uniform_bool(passthough_type->flip, cache->effect.flipped);
    shader_set_future_uniform_float(passthough_type->opacity, w->opacity / 100.0);
    shader_set_future_uniform_sampler(passthough_type->tex_scr, 0);
    shader_use(passthough_program);

    {
        Vector2 rpos = *pos;
        vec2_sub(&rpos, &cache->border);
        Vector3 tdrpos = vec3_from_vec2(&rpos, z);
        Vector2 rsize = cache->texture.size;

        texture_bind(&cache->effect, GL_TEXTURE0);

        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);

        draw_rect(w->face, passthough_type->mvp, tdrpos, rsize);

        glDepthMask(GL_TRUE);
        glDisable(GL_DEPTH_TEST);
    }

    glDisable(GL_BLEND);
}
