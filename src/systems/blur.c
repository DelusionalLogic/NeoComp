#define GL_GLEXT_PROTOTYPES
#include "blur.h"

#include "profiler/zone.h"

#include "assets/assets.h"
#include "assets/shader.h"
#include "shaders/shaderinfo.h"
#include "renderutil.h"
#include "window.h"
#include "textureeffects.h"
#include "framebuffer.h"

#include <stdio.h>

DECLARE_ZONE(update_blur);
DECLARE_ZONE(fetch_candidates);

// @CUTNPASTE: This has been copied around a bunch. Maybe it needs someplace
// for itself.
static Vector2 X11_rectpos_to_gl(const Vector2* rootsize, const Vector2* xpos, const Vector2* size) {
    Vector2 glpos = {{
        xpos->x, rootsize->y - xpos->y - size->y
    }};
    return glpos;
}

static void fetchSortedWindowsWithArr(Swiss* em, Vector* result, CType* query) {
    for_componentsArr(it, em, query) {
        vector_putBack(result, &it.id);
    }
    vector_qsort(result, window_zcmp, em);
}
#define fetchSortedWindowsWith(em, result, ...) \
    fetchSortedWindowsWithArr(em, result, (CType[]){ __VA_ARGS__ })

void blur_init(struct blur* blur) {
    glGenVertexArrays(1, &blur->array);
    glBindVertexArray(blur->array);

    // Generate FBO if needed
    if(!framebuffer_initialized(&blur->fbo)) {
        if(!framebuffer_init(&blur->fbo)) {
            printf("Failed allocating framebuffer for cache\n");
            return;
        }
    }
    vector_init(&blur->to_blur, sizeof(win_id), 128);
    vector_init(&blur->opaque_renderable, sizeof(win_id), 128);
    vector_init(&blur->shadow_renderable, sizeof(win_id), 128);
    vector_init(&blur->transparent_renderable, sizeof(win_id), 128);

	vector_init(&blur->opaque_behind, sizeof(win_id), 16);
	vector_init(&blur->transparent_behind, sizeof(win_id), 16);
}

void blur_destroy(struct blur* blur) {
    glDeleteVertexArrays(1, &blur->array);
    vector_kill(&blur->to_blur);
    vector_kill(&blur->opaque_renderable);
    vector_kill(&blur->shadow_renderable);
    vector_kill(&blur->transparent_renderable);

	vector_kill(&blur->opaque_behind);
	vector_kill(&blur->transparent_behind);
}

void blursystem_updateBlur(struct blur* gblur, Swiss* em, Vector2* root_size,
        struct Texture* texture, int level, struct _session* ps) {
    for_components(it, em,
            COMPONENT_MUD, COMPONENT_MAP, COMPONENT_TEXTURED, CQ_NOT, COMPONENT_BLUR, CQ_END) {
        struct _win* w = swiss_getComponent(em, COMPONENT_MUD, it.id);

        struct glx_blur_cache* blur = swiss_addComponent(em, COMPONENT_BLUR, it.id);

        if(blur_cache_init(blur) != 0) {
            printf_errf("Failed initializing window blur");
            swiss_removeComponent(em, COMPONENT_BLUR, it.id);
        }
    }

    for_components(it, em,
            COMPONENT_MAP, COMPONENT_BLUR, CQ_END) {
        struct MapComponent* map = swiss_getComponent(em, COMPONENT_MAP, it.id);
        struct glx_blur_cache* blur = swiss_getComponent(em, COMPONENT_BLUR, it.id);

        if(!blur_cache_resize(blur, &map->size)) {
            printf_errf("Failed resizing window blur");
        }
        swiss_ensureComponent(em, COMPONENT_BLUR_DAMAGED, it.id);
    }


    zone_scope(&ZONE_update_blur);

    {
        zone_scope(&ZONE_fetch_candidates);
        vector_clear(&gblur->to_blur);
        fetchSortedWindowsWith(em, &gblur->to_blur, 
                COMPONENT_MUD, COMPONENT_BLUR, COMPONENT_BLUR_DAMAGED, COMPONENT_Z,
                COMPONENT_PHYSICAL, CQ_END);

        vector_clear(&gblur->opaque_renderable);
        fetchSortedWindowsWith(em, &gblur->opaque_renderable, 
                COMPONENT_MUD, COMPONENT_TEXTURED, COMPONENT_Z, COMPONENT_PHYSICAL,
                CQ_NOT, COMPONENT_OPACITY, CQ_END);

        vector_clear(&gblur->shadow_renderable);
        fetchSortedWindowsWith(em, &gblur->shadow_renderable, 
                COMPONENT_MUD, COMPONENT_SHADOW, COMPONENT_Z, COMPONENT_PHYSICAL,
                CQ_NOT, COMPONENT_OPACITY, CQ_END);

        // @PERFORMANCE: We should probably restrict these windows to only those
        // that could possibly do something in drawTransparent. for that we need
        // some way to merge vectors.
        vector_clear(&gblur->transparent_renderable);
        fetchSortedWindowsWith(em, &gblur->transparent_renderable, 
                COMPONENT_MUD, COMPONENT_Z, COMPONENT_PHYSICAL,
                /* COMPONENT_OPACITY, */ CQ_END);
    }

    framebuffer_resetTarget(&gblur->fbo);
    framebuffer_bind(&gblur->fbo);

    struct face* face = assets_load("window.face");

    glDisable(GL_STENCIL_TEST);
    glDisable(GL_SCISSOR_TEST);

    // Blurring is a strange process, because every window depends on the blurs
    // behind it. Therefore we render them individually, starting from the
    // back.
    size_t index;
    win_id* w_id = vector_getLast(&gblur->to_blur, &index);
    while(w_id != NULL) {
        struct PhysicalComponent* physical = swiss_getComponent(em, COMPONENT_PHYSICAL, *w_id);
        struct glx_blur_cache* blur = swiss_getComponent(em, COMPONENT_BLUR, *w_id);

        Vector2 glpos = X11_rectpos_to_gl(root_size, &physical->position, &physical->size);

        struct Texture* tex = &blur->texture[1];

        framebuffer_resetTarget(&gblur->fbo);
        framebuffer_targetRenderBuffer_stencil(&gblur->fbo, &blur->stencil);
        framebuffer_targetTexture(&gblur->fbo, tex);
        framebuffer_rebind(&gblur->fbo);

        Matrix old_view = view;
        view = mat4_orthogonal(glpos.x, glpos.x + physical->size.x, glpos.y, glpos.y + physical->size.y, -1, 1);
        glViewport(0, 0, physical->size.x, physical->size.y);

        glEnable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);

        glClearColor(1.0, 0.0, 1.0, 0.0);
        glClearDepth(1.0);
        glDepthMask(GL_TRUE);
        glDepthFunc(GL_LESS);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // Find the drawables behind this one
        vector_clear(&gblur->opaque_behind);
        windowlist_findbehind(em, &gblur->opaque_renderable, *w_id, &gblur->opaque_behind);
        vector_clear(&gblur->transparent_behind);
        windowlist_findbehind(em, &gblur->transparent_renderable, *w_id, &gblur->transparent_behind);

        windowlist_drawBackground(ps, &gblur->opaque_behind);
        windowlist_draw(ps, &gblur->opaque_behind);

        // Draw root
        glEnable(GL_DEPTH_TEST);
        draw_tex(face, texture, &(Vector3){{0, 0, 0.99999}}, root_size);
        glDisable(GL_DEPTH_TEST);

        windowlist_drawTransparent(ps, &gblur->transparent_behind);

        view = old_view;

        glDisable(GL_BLEND);

        struct TextureBlurData blurData = {
            .depth = &blur->stencil,
            .tex = tex,
            .swap = &blur->texture[0],
        };

        // @PERFORMANCE: I think we could do some batching here as long as the
        // windows don't overlap
        if(!texture_blur(&blurData, &gblur->fbo, level, false)) {
            printf_errf("Failed blurring the background texture\n");
            return;
        }

        // Flip the blur back into texture[0] to clip to the stencil
        framebuffer_resetTarget(&gblur->fbo);
        framebuffer_targetTexture(&gblur->fbo, &blur->texture[0]);
        if(framebuffer_rebind(&gblur->fbo) != 0) {
            printf("Failed binding framebuffer to clip blur\n");
            return;
        }

        old_view = view;
        view = mat4_orthogonal(0, blur->texture[0].size.x, 0, blur->texture[0].size.y, -1, 1);
        glViewport(0, 0, blur->texture[0].size.x, blur->texture[0].size.y);

        glClearColor(0.0, 0.0, 0.0, 0.0);
        glClear(GL_COLOR_BUFFER_BIT);

        /* glEnable(GL_STENCIL_TEST); */

        glStencilMask(0);
        glStencilFunc(GL_EQUAL, 1, 0xFF);

        draw_tex(face, &blur->texture[1], &VEC3_ZERO, &blur->texture[0].size);

        /* glDisable(GL_STENCIL_TEST); */
        view = old_view;

        w_id = vector_getPrev(&gblur->to_blur, &index);
    }

    swiss_resetComponent(em, COMPONENT_BLUR_DAMAGED);
}

bool blur_cache_resize(glx_blur_cache_t* cache, const Vector2* size) {
    assert(renderbuffer_initialized(&cache->stencil));
    assert(texture_initialized(&cache->texture[0]));
    assert(texture_initialized(&cache->texture[1]));

    cache->size = *size;

    renderbuffer_resize(&cache->stencil, size);
    texture_resize(&cache->texture[0], size);
    texture_resize(&cache->texture[1], size);
    return true;
}

int blur_cache_init(glx_blur_cache_t* cache) {
    assert(!renderbuffer_initialized(&cache->stencil));
    assert(!texture_initialized(&cache->texture[0]));
    assert(!texture_initialized(&cache->texture[1]));

    if(renderbuffer_stencil_init(&cache->stencil, NULL) != 0) {
        printf("Failed allocating stencil for cache\n");
        return 1;
    }

    if(texture_init(&cache->texture[0], GL_TEXTURE_2D, NULL) != 0) {
        printf("Failed allocating texture for cache\n");
        renderbuffer_delete(&cache->stencil);
        return 1;
    }

    if(texture_init(&cache->texture[1], GL_TEXTURE_2D, NULL) != 0) {
        printf("Failed allocating texture for cache\n");
        renderbuffer_delete(&cache->stencil);
        texture_delete(&cache->texture[0]);
        return 1;
    }

    return 0;
}

void blur_cache_delete(glx_blur_cache_t* cache) {
    assert(renderbuffer_initialized(&cache->stencil));
    assert(texture_initialized(&cache->texture[0]));
    assert(texture_initialized(&cache->texture[1]));

    renderbuffer_delete(&cache->stencil);
    texture_delete(&cache->texture[0]);
    texture_delete(&cache->texture[1]);
}
