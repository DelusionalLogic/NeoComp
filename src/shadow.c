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
    glx_mark(ps, 0, true);
    win_id wid = swiss_indexOfPointer(&ps->win_list, COMPONENT_MUD, w);
    if(!swiss_hasComponent(&ps->win_list, COMPONENT_SHADOW, wid)) {
        return;
    }

    struct glx_shadow_cache* cache = swiss_getComponent(&ps->win_list, COMPONENT_SHADOW, wid);

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

static bool win_viewable(win* w) {
    return w->state == STATE_DEACTIVATING || w->state == STATE_ACTIVATING
        || w->state == STATE_ACTIVE || w->state == STATE_INACTIVE
        || w->state == STATE_HIDING || w->state == STATE_DESTROYING;
}

void windowlist_updateShadow(session_t* ps, Vector* paints) {
    Vector shadow_updates;
    vector_init(&shadow_updates, sizeof(win_id), paints->size);


    // @HACK: For legacy reasons we assume the shadow is damaged if the size of
    // the window has changed. we should move over to manually damaging it when
    // we change size
    {
        static const enum ComponentType req_types[] = {
            COMPONENT_MUD,
            COMPONENT_SHADOW,
            0
        };
        struct SwissIterator it = {0};
        swiss_getFirst(&ps->win_list, req_types, &it);
        while(!it.done) {
            struct _win* w = swiss_getComponent(&ps->win_list, COMPONENT_MUD, it.id);
            struct glx_shadow_cache* shadow = swiss_getComponent(&ps->win_list, COMPONENT_SHADOW, it.id);

            if(w->state == STATE_DESTROYED || w->state == STATE_INVISIBLE) {
                swiss_getNext(&ps->win_list, req_types, &it);
                continue;
            }

            if(!swiss_hasComponent(&ps->win_list, COMPONENT_SHADOW_DAMAGED, it.id))
                    swiss_addComponent(&ps->win_list, COMPONENT_SHADOW_DAMAGED, it.id);

            swiss_getNext(&ps->win_list, req_types, &it);
        }
    }

    struct Framebuffer framebuffer;
    if(!framebuffer_init(&framebuffer)) {
        printf("Couldn't create framebuffer for shadow\n");
        return;
    }
    framebuffer_resetTarget(&framebuffer);
    framebuffer_bind(&framebuffer);

    Vector blurDatas;
    vector_init(&blurDatas, sizeof(struct TextureBlurData), ps->win_list.size);

    glDisable(GL_BLEND);
    glEnable(GL_STENCIL_TEST);

    glClearColor(0.0, 0.0, 0.0, 0.0);

    glStencilMask(0xFF);
    glClearStencil(0);
    glStencilFunc(GL_EQUAL, 0, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_INCR);

    static const enum ComponentType req_types[] = {
        COMPONENT_MUD,
        COMPONENT_SHADOW_DAMAGED,
        COMPONENT_SHADOW,
        0
    };
    struct SwissIterator it = {0};
    swiss_getFirst(&ps->win_list, req_types, &it);
    while(!it.done) {
        struct _win* w = swiss_getComponent(&ps->win_list, COMPONENT_MUD, it.id);
        struct glx_shadow_cache* shadow = swiss_getComponent(&ps->win_list, COMPONENT_SHADOW, it.id);

        Vector2 size = {{w->widthb, w->heightb}};
        shadow_cache_resize(shadow, &size);

        framebuffer_resetTarget(&framebuffer);
        framebuffer_targetTexture(&framebuffer, &shadow->texture);
        framebuffer_targetRenderBuffer_stencil(&framebuffer, &shadow->stencil);
        framebuffer_rebind(&framebuffer);

        Matrix old_view = view;
        view = mat4_orthogonal(0, shadow->texture.size.x, 0, shadow->texture.size.y, -1, 1);

        glViewport(0, 0, shadow->texture.size.x, shadow->texture.size.y);

        glClear(GL_STENCIL_BUFFER_BIT | GL_COLOR_BUFFER_BIT);

        assert(w->drawable.bound);
        texture_bind(&w->drawable.texture, GL_TEXTURE0);

        struct shader_program* shadow_program = assets_load("shadow.shader");
        if(shadow_program->shader_type_info != &shadow_info) {
            printf_errf("Shader was not a shadow shader\n");
            framebuffer_delete(&framebuffer);
            view = old_view;
            return;
        }
        struct Shadow* shadow_type = shadow_program->shader_type;

        shader_set_future_uniform_bool(shadow_type->flip, w->drawable.texture.flipped);
        shader_set_future_uniform_sampler(shadow_type->tex_scr, 0);

        shader_use(shadow_program);

        Vector3 pos = vec3_from_vec2(&shadow->border, 0.0);
        draw_rect(w->face, shadow_type->mvp, pos, size);

        view = old_view;

        // Do the blur
        struct TextureBlurData blurData = {
            .depth = &shadow->stencil,
            .tex = &shadow->texture,
            .swap = &shadow->effect,
        };
        vector_putBack(&blurDatas, &blurData);

        swiss_getNext(&ps->win_list, req_types, &it);
    }

    glDisable(GL_STENCIL_TEST);

    textures_blur(&blurDatas, &framebuffer, 3, false);

    vector_kill(&blurDatas);

    framebuffer_resetTarget(&framebuffer);
    if(framebuffer_bind(&framebuffer) != 0) {
        printf("Failed binding framebuffer to clip shadow\n");
    }

    glClearColor(0.0, 0.0, 0.0, 0.0);
    glStencilMask(0xFF);
    glStencilFunc(GL_EQUAL, 0, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);

    glEnable(GL_STENCIL_TEST);

    swiss_getFirst(&ps->win_list, req_types, &it);
    while(!it.done) {
        struct _win* w = swiss_getComponent(&ps->win_list, COMPONENT_MUD, it.id);
        struct glx_shadow_cache* shadow = swiss_getComponent(&ps->win_list, COMPONENT_SHADOW, it.id);

        framebuffer_resetTarget(&framebuffer);
        framebuffer_targetTexture(&framebuffer, &shadow->effect);
        framebuffer_targetRenderBuffer_stencil(&framebuffer, &shadow->stencil);
        if(framebuffer_rebind(&framebuffer) != 0) {
            printf("Failed binding framebuffer to clip shadow\n");
            return;
        }

        Matrix old_view = view;
        view = mat4_orthogonal(0, shadow->effect.size.x, 0, shadow->effect.size.y, -1, 1);
        glViewport(0, 0, shadow->effect.size.x, shadow->effect.size.y);

        glClear(GL_COLOR_BUFFER_BIT);

        draw_tex(w->face, &shadow->texture, &VEC3_ZERO, &shadow->effect.size);

        view = old_view;

        swiss_removeComponent(&ps->win_list, COMPONENT_SHADOW_DAMAGED, it.id);
        swiss_getNext(&ps->win_list, req_types, &it);
    }

    glDisable(GL_STENCIL_TEST);

    vector_kill(&shadow_updates);
    framebuffer_delete(&framebuffer);
}

