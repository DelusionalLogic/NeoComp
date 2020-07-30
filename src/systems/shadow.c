#include "shadow.h"

#include "assets/assets.h"
#include "assets/shader.h"
#include "shaders/shaderinfo.h"
#include "textureeffects.h"

#include "renderutil.h"

#include "profiler/zone.h"

#include <assert.h>

DECLARE_ZONE(shadow_clear);
DECLARE_ZONE(shadow_copy);
DECLARE_ZONE(shadow_setup_blur);
DECLARE_ZONE(shadow_blur);
DECLARE_ZONE(shadow_clip_create);
DECLARE_ZONE(shadow_clip);
DECLARE_ZONE(update_shadow);

#define SHADOW_RADIUS 64

int shadow_cache_init(struct glx_shadow_cache* cache) {
    Vector2 border = {{SHADOW_RADIUS, SHADOW_RADIUS}};
    cache->border = border;

    if(texture_init_hp(&cache->texture, GL_TEXTURE_2D, NULL) != 0) {
        printf("Couldn't create texture for shadow\n");
        return 1;
    }

    if(texture_init_hp(&cache->effect, GL_TEXTURE_2D, NULL) != 0) {
        printf("Couldn't create effect texture for shadow\n");
        texture_delete(&cache->texture);
        return 1;
    }

    if(texture_init_noise(&cache->noise, GL_TEXTURE_2D) != 0) {
        printf("Couldn't create noiseatexture for shadow\n");
        texture_delete(&cache->texture);
        texture_delete(&cache->effect);
        return 1;
    }

    if(renderbuffer_stencil_init(&cache->stencil, NULL) != 0) {
        printf("Couldn't create renderbuffer stencil for shadow\n");
        texture_delete(&cache->texture);
        texture_delete(&cache->effect);
        texture_delete(&cache->noise);
        return 1;
    }
    cache->initialized = true;
    return 0;
}

int shadow_cache_resize(struct glx_shadow_cache* cache, const Vector2* size) {
    assert(cache->initialized == true);
    cache->wSize = *size;

    Vector2 overflowSize = cache->border;
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

void shadowsystem_delete(Swiss *em) {
    for_components(it, em,
            COMPONENT_SHADOW, CQ_END) {
        struct glx_shadow_cache* shadow = swiss_getComponent(em, COMPONENT_SHADOW, it.id);
        shadow_cache_delete(shadow);
    }
    swiss_resetComponent(em, COMPONENT_SHADOW);
}

void shadowsystem_tick(Swiss* em) {
    for_components(it, em,
            COMPONENT_RESIZE, COMPONENT_SHADOW, COMPONENT_CONTENTS_DAMAGED, CQ_END) {
        struct ResizeComponent* resize = swiss_getComponent(em, COMPONENT_RESIZE, it.id);
        struct glx_shadow_cache* shadow = swiss_getComponent(em, COMPONENT_SHADOW, it.id);

        shadow_cache_resize(shadow, &resize->newSize);
        swiss_ensureComponent(em, COMPONENT_SHADOW_DAMAGED, it.id);
    }

    for_components(it, em, COMPONENT_STATEFUL, COMPONENT_SHADOW, CQ_END) {
        struct glx_shadow_cache* shadow = swiss_getComponent(em, COMPONENT_SHADOW, it.id);
        struct StatefulComponent* stateful = swiss_getComponent(em, COMPONENT_STATEFUL, it.id);

        if(stateful->state == STATE_DESTROYED || stateful->state == STATE_INVISIBLE) {
            shadow_cache_delete(shadow);
            swiss_removeComponent(em, COMPONENT_SHADOW, it.id);
        }
    }
}

void shadowsystem_updateShadow(session_t* ps, Vector* paints) {
    Swiss* em = &ps->win_list;

    for_components(it, em,
            COMPONENT_MUD, COMPONENT_MAP, COMPONENT_TEXTURED, CQ_NOT, COMPONENT_SHADOW, CQ_END) {
        struct _win* w = swiss_getComponent(em, COMPONENT_MUD, it.id);

        // Legacy value for disabling shadows
        if(w->shadow && ps->o.wintype_shadow[w->window_type]) {
            struct glx_shadow_cache* shadow = swiss_addComponent(em, COMPONENT_SHADOW, it.id);

            if(shadow_cache_init(shadow) != 0) {
                printf_errf("Failed initializing window shadow");
                swiss_removeComponent(em, COMPONENT_SHADOW, it.id);
            }
        }
    }

    for_components(it, em,
            COMPONENT_MAP, COMPONENT_PHYSICAL, COMPONENT_SHADOW, CQ_END) {
        struct PhysicalComponent* phy = swiss_getComponent(em, COMPONENT_PHYSICAL, it.id);
        struct glx_shadow_cache* shadow = swiss_getComponent(em, COMPONENT_SHADOW, it.id);

        shadow_cache_resize(shadow, &phy->size);
        // Since the cache was resized, the shadow will have to be recalculated
        swiss_ensureComponent(em, COMPONENT_SHADOW_DAMAGED, it.id);
    }

    zone_scope(&ZONE_update_shadow);

    Vector shadow_updates;
    vector_init(&shadow_updates, sizeof(win_id), paints->size);

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

    glStencilMask(0xFF);
    glClearColor(0.0, 0.0, 0.0, 0.0);
    glClearStencil(0);

    // Clear all the shadow textures we are about to render into
    for_components(it, &ps->win_list,
        COMPONENT_MUD, COMPONENT_TEXTURED, COMPONENT_PHYSICAL, COMPONENT_SHADOW_DAMAGED, COMPONENT_SHADOW,
        COMPONENT_SHAPED, CQ_END) {
        zone_scope(&ZONE_shadow_clear);
        struct glx_shadow_cache* shadow = swiss_getComponent(&ps->win_list, COMPONENT_SHADOW, it.id);

        framebuffer_resetTarget(&framebuffer);
        framebuffer_targetTexture(&framebuffer, &shadow->texture);
        framebuffer_targetRenderBuffer_stencil(&framebuffer, &shadow->stencil);
        framebuffer_rebind(&framebuffer);

        glViewport(0, 0, shadow->texture.size.x, shadow->texture.size.y);

        glClear(GL_STENCIL_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
    }


    struct shader_program* shadow_program = assets_load("shadow.shader");
    if(shadow_program->shader_type_info != &shadow_info) {
        printf_errf("Shader was not a shadow shader\n");
        framebuffer_delete(&framebuffer);
        return;
    }
    struct Shadow* shadow_type = shadow_program->shader_type;
    shader_set_future_uniform_bool(shadow_type->flip, false);
    shader_set_future_uniform_sampler(shadow_type->tex_scr, 0);

    shader_use(shadow_program);

    Matrix old_view = view;

    // Render into the textures
    for_components(it, &ps->win_list,
        COMPONENT_MUD, COMPONENT_TEXTURED, COMPONENT_PHYSICAL, COMPONENT_SHADOW_DAMAGED, COMPONENT_SHADOW,
        COMPONENT_SHAPED, CQ_END) {
        zone_scope(&ZONE_shadow_copy);
        struct TexturedComponent* textured = swiss_getComponent(&ps->win_list, COMPONENT_TEXTURED, it.id);
        struct PhysicalComponent* physical = swiss_getComponent(&ps->win_list, COMPONENT_PHYSICAL, it.id);
        struct glx_shadow_cache* shadow = swiss_getComponent(&ps->win_list, COMPONENT_SHADOW, it.id);
        struct ShapedComponent* shaped = swiss_getComponent(&ps->win_list, COMPONENT_SHAPED, it.id);

        framebuffer_resetTarget(&framebuffer);
        framebuffer_targetTexture(&framebuffer, &shadow->texture);
        framebuffer_targetRenderBuffer_stencil(&framebuffer, &shadow->stencil);
        framebuffer_rebind(&framebuffer);

        view = mat4_orthogonal(0, shadow->texture.size.x, 0, shadow->texture.size.y, -1, 1);

        glViewport(0, 0, shadow->texture.size.x, shadow->texture.size.y);

        texture_bind(&textured->texture, GL_TEXTURE0);

        shader_set_uniform_bool(shadow_type->flip, textured->texture.flipped);

        Vector3 pos = vec3_from_vec2(&shadow->border, 0.0);
        draw_rect(shaped->face, shadow_type->mvp, pos, physical->size);

    }

    view = old_view;

    // Setup the blur request data
    for_components(it, &ps->win_list,
        COMPONENT_MUD, COMPONENT_TEXTURED, COMPONENT_PHYSICAL, COMPONENT_SHADOW_DAMAGED, COMPONENT_SHADOW,
        COMPONENT_SHAPED, CQ_END) {
        zone_scope(&ZONE_shadow_setup_blur);
        struct glx_shadow_cache* shadow = swiss_getComponent(&ps->win_list, COMPONENT_SHADOW, it.id);

        struct TextureBlurData blurData = {
            .depth = &shadow->stencil,
            .tex = &shadow->texture,
            .swap = &shadow->effect,
        };
        vector_putBack(&blurDatas, &blurData);
    }

    glDisable(GL_STENCIL_TEST);

    zone_enter(&ZONE_shadow_blur);
    textures_blur(&blurDatas, &framebuffer, 4, false);
    zone_leave(&ZONE_shadow_blur);

    vector_kill(&blurDatas);

    framebuffer_resetTarget(&framebuffer);
    if(framebuffer_bind(&framebuffer) != 0) {
        printf("Failed binding framebuffer to clip shadow\n");
    }

    struct face* face = assets_load("window.face");

    struct shader_program* shader = assets_load("postshadow.shader");
    if(shader->shader_type_info != &postshadow_info) {
        printf_errf("Shader was not a postshadow shader\n");
        return;
    }
    struct PostShadow* shader_type = shader->shader_type;
    shader_set_future_uniform_sampler(shader_type->tex_scr, 0);
    shader_set_future_uniform_sampler(shader_type->noise_scr, 1);

    shader_use(shader);

    old_view = view;
    for_components(it, &ps->win_list,
        COMPONENT_MUD, COMPONENT_TEXTURED, COMPONENT_PHYSICAL, COMPONENT_SHADOW_DAMAGED, COMPONENT_SHADOW,
        COMPONENT_SHAPED, CQ_END) {
        zone_scope(&ZONE_shadow_clip);
        struct glx_shadow_cache* shadow = swiss_getComponent(&ps->win_list, COMPONENT_SHADOW, it.id);

        framebuffer_resetTarget(&framebuffer);
        framebuffer_targetTexture(&framebuffer, &shadow->effect);
        framebuffer_targetRenderBuffer_stencil(&framebuffer, &shadow->stencil);
        if(framebuffer_rebind(&framebuffer) != 0) {
            printf("Failed binding framebuffer to clip shadow\n");
            continue;
        }

        view = mat4_orthogonal(0, shadow->effect.size.x, 0, shadow->effect.size.y, -1, 1);
        glViewport(0, 0, shadow->effect.size.x, shadow->effect.size.y);

        glClear(GL_COLOR_BUFFER_BIT);

        shader_set_uniform_bool(shader_type->flip, shadow->texture.flipped);

        texture_bind(&shadow->texture, GL_TEXTURE0);
        texture_bind(&shadow->noise, GL_TEXTURE1);

        draw_rect(face, shader_type->mvp, VEC3_ZERO, shadow->effect.size);

    }
    view = old_view;

    swiss_resetComponent(&ps->win_list, COMPONENT_SHADOW_DAMAGED);

    vector_kill(&shadow_updates);
    framebuffer_delete(&framebuffer);
}
