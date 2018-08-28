#include "windowlist.h"

#include "profiler/zone.h"

#include "assets/shader.h"
#include "assets/assets.h"

#include "shaders/shaderinfo.h"

#include "textureeffects.h"

#include "window.h"
#include "blur.h"
#include "renderutil.h"

DECLARE_ZONE(paint_window);

static bool win_viewable(win* w) {
    return w->state == STATE_DEACTIVATING || w->state == STATE_ACTIVATING
        || w->state == STATE_ACTIVE || w->state == STATE_INACTIVE
        || w->state == STATE_HIDING || w->state == STATE_DESTROYING;
}


void windowlist_draw(session_t* ps, Vector* paints, float* z) {
    glx_mark(ps, 0, true);
    (*z) = 1;
    glEnable(GL_DEPTH_TEST);
    size_t index;
    win_id* w_id = vector_getFirst(paints, &index);
    while(w_id != NULL) {
        struct _win* w = swiss_getComponent(&ps->win_list, COMPONENT_MUD, *w_id);
        w->z = *z;

        zone_enter_extra(&ZONE_paint_window, "%s", w->name);
        if(w->state == STATE_DESTROYING || w->state == STATE_HIDING
                || w->state == STATE_ACTIVATING || w->state == STATE_DEACTIVATING
                || w->state == STATE_ACTIVE || w->state == STATE_INACTIVE) {
            win_draw(ps, w, w->z);
        }
        zone_leave(&ZONE_paint_window);

        // @HACK: This shouldn't be hardcoded. As it stands, it will probably break
        // for more than 1k windows
        (*z) -= .0001;
        w_id = vector_getNext(paints, &index);
    }
    glx_mark(ps, 0, false);
}

void windowlist_findbehind(const Swiss* win_list, const Vector* paints, const win* overlap, const size_t overlap_index, Vector* overlaps) {
    size_t index = overlap_index;
    win_id* w_id = vector_getNext(paints, &index);
    while(w_id != NULL) {
        struct _win* w = swiss_getComponent(win_list, COMPONENT_MUD, *w_id);

        if(win_overlap(overlap, w)) {
            vector_putBack(overlaps, w_id);
        }

        w_id = vector_getNext(paints, &index);
    }
}

static Vector2 X11_rectpos_to_gl(session_t *ps, const Vector2* xpos, const Vector2* size) {
    Vector2 glpos = {{
        xpos->x, ps->root_height - xpos->y - size->y
    }};
    return glpos;
}

void windowlist_updateBlur(session_t* ps, Vector* paints) {

    struct blur* blur = &ps->psglx->blur;

    framebuffer_resetTarget(&blur->fbo);
    framebuffer_bind(&blur->fbo);

    struct face* face = assets_load("window.face");

    glDisable(GL_STENCIL_TEST);
    glDisable(GL_SCISSOR_TEST);

    size_t index;
    win_id* w_id = vector_getLast(paints, &index);
    while(w_id != NULL) {
        struct _win* w = swiss_getComponent(&ps->win_list, COMPONENT_MUD, *w_id);
        struct PhysicalComponent* physical = swiss_getComponent(&ps->win_list, COMPONENT_PHYSICAL, *w_id);

        Vector2 glpos = X11_rectpos_to_gl(ps, &physical->position, &physical->size);

        if(win_viewable(w)) {
            if (w->blur_background && (!w->solid || ps->o.blur_background_frame)) {
                if(swiss_hasComponent(&ps->win_list, COMPONENT_BLUR_DAMAGED, *w_id)) {

                    struct Texture* tex = &w->glx_blur_cache.texture[1];

                    framebuffer_resetTarget(&blur->fbo);
                    framebuffer_targetRenderBuffer_stencil(&blur->fbo, &w->glx_blur_cache.stencil);
                    framebuffer_targetTexture(&blur->fbo, tex);
                    framebuffer_rebind(&blur->fbo);

                    glDepthMask(GL_TRUE);

                    glClearColor(1.0, 0.0, 1.0, 0.0);

                    glEnable(GL_DEPTH_TEST);
                    glEnable(GL_BLEND);

                    glClearDepth(0.0);
                    glDepthFunc(GL_GREATER);

                    glViewport(0, 0, physical->size.x, physical->size.y);
                    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

                    Matrix old_view = view;
                    view = mat4_orthogonal(glpos.x, glpos.x + physical->size.x, glpos.y, glpos.y + physical->size.y, -1, 1);

                    Vector behind;
                    vector_init(&behind, sizeof(win_id), vector_size(paints) - index);

                    windowlist_findbehind(&ps->win_list, paints, w, index, &behind);

                    float z = 0;
                    windowlist_draw(ps, &behind, &z);

                    vector_kill(&behind);

                    Vector2 root_size = {{ps->root_width, ps->root_height}};

                    draw_tex(face, &ps->root_texture.texture, &VEC3_ZERO, &root_size);

                    view = old_view;

                    glDisable(GL_BLEND);

                    int level = ps->o.blur_level;

                    struct TextureBlurData blurData = {
                        .depth = &w->glx_blur_cache.stencil,
                        .tex = tex,
                        .swap = &w->glx_blur_cache.texture[0],
                    };
                    // Do the blur
                    if(!texture_blur(&blurData, &blur->fbo, level, false)) {
                        printf_errf("Failed blurring the background texture\n");
                        return;
                    }

                    // Flip the blur back into texture[0] to clip to the stencil
                    framebuffer_resetTarget(&blur->fbo);
                    framebuffer_targetTexture(&blur->fbo, &w->glx_blur_cache.texture[0]);
                    if(framebuffer_rebind(&blur->fbo) != 0) {
                        printf("Failed binding framebuffer to clip blur\n");
                        return;
                    }

                    old_view = view;
                    view = mat4_orthogonal(0, w->glx_blur_cache.texture[0].size.x, 0, w->glx_blur_cache.texture[0].size.y, -1, 1);
                    glViewport(0, 0, w->glx_blur_cache.texture[0].size.x, w->glx_blur_cache.texture[0].size.y);

                    glClearColor(0.0, 0.0, 0.0, 0.0);
                    glClear(GL_COLOR_BUFFER_BIT);

                    /* glEnable(GL_STENCIL_TEST); */

                    glStencilMask(0);
                    glStencilFunc(GL_EQUAL, 1, 0xFF);

                    draw_tex(face, &w->glx_blur_cache.texture[1], &VEC3_ZERO, &w->glx_blur_cache.texture[0].size);

                    /* glDisable(GL_STENCIL_TEST); */
                    view = old_view;

                }
            }
        }

        w_id = vector_getPrev(paints, &index);
    }

    swiss_resetComponent(&ps->win_list, COMPONENT_BLUR_DAMAGED);
}
