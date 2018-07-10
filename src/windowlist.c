#include "windowlist.h"

#include "profiler/zone.h"
#include "assets/shader.h"
#include "assets/assets.h"
#include "shaders/shaderinfo.h"

#include "window.h"

DECLARE_ZONE(paint_window);

static bool win_viewable(win* w) {
    return w->state == STATE_DEACTIVATING || w->state == STATE_ACTIVATING
        || w->state == STATE_ACTIVE || w->state == STATE_INACTIVE
        || w->state == STATE_HIDING || w->state == STATE_DESTROYING;
}


void windowlist_draw(session_t* ps, win* head, float* z) {
    glx_mark(ps, head->id, true);
    (*z) = 1;
    glEnable(GL_DEPTH_TEST);
    for (win *w = head; w; w = w->next_trans) {

        zone_enter_extra(&ZONE_paint_window, "%s", w->name);
        if(w->state == STATE_DESTROYING || w->state == STATE_HIDING
                || w->state == STATE_ACTIVATING || w->state == STATE_DEACTIVATING
                || w->state == STATE_ACTIVE || w->state == STATE_INACTIVE) {
            win_draw(ps, w, *z);
        }
        zone_leave(&ZONE_paint_window);

        // @HACK: This shouldn't be hardcoded. As it stands, it will probably break
        // for more than 1k windows
        (*z) -= .0001;
    }
    glx_mark(ps, head->id, false);
}

void windowlist_drawoverlap(session_t* ps, win* head, win* overlap, float* z) {
    glx_mark(ps, head->id, true);
    (*z) = 1;
    glEnable(GL_DEPTH_TEST);
    for (win *w = head; w; w = w->next_trans) {
        if(!win_overlap(overlap, w))
            continue;

        if(w->state == STATE_DESTROYING || w->state == STATE_HIDING
                || w->state == STATE_ACTIVATING || w->state == STATE_DEACTIVATING
                || w->state == STATE_ACTIVE || w->state == STATE_INACTIVE) {
            win_draw(ps, w, *z);
        }

        // @HACK: This shouldn't be hardcoded. As it stands, it will probably break
        // for more than 1k windows
        (*z) -= .0001;
    }
    glx_mark(ps, head->id, false);
}

void windowlist_updateStencil(session_t* ps, Vector* paints) {
    glEnable(GL_STENCIL_TEST);
    glDisable(GL_DEPTH_TEST);

    glDepthMask(GL_FALSE);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);

    glStencilMask(0xFF);
    glClearStencil(0);
    glStencilFunc(GL_NEVER, 1, 0xFF);
    glStencilOp(GL_REPLACE, GL_KEEP, GL_KEEP);

    struct shader_program* program = assets_load("stencil.shader");
    if(program->shader_type_info != &stencil_info) {
        printf_errf("Shader was not a stencil shader\n");
        return;
    }
    struct Stencil* type = program->shader_type;
    shader_set_future_uniform_sampler(type->tex_scr, 0);

    shader_use(program);

    size_t index;
    win_id* w_id = vector_getLast(paints, &index);
    while(w_id != NULL) {
        struct _win* w = swiss_get(&ps->win_list, *w_id);
        if(win_viewable(w) && ps->redirected) {
            Vector2 size = {{w->widthb, w->heightb}};

            if (w->stencil_damaged) {
                Vector3 dglPos = {{0, 0, 0}};

                framebuffer_resetTarget(&ps->psglx->stencil_fbo);
                framebuffer_targetRenderBuffer_stencil(&ps->psglx->stencil_fbo, &w->stencil);
                if(framebuffer_bind(&ps->psglx->stencil_fbo) != 0) {
                    printf("Failed binding framebuffer for stencil\n");
                    return;
                }
                shader_set_uniform_bool(type->flip, w->drawable.texture.flipped);

                glClear(GL_STENCIL_BUFFER_BIT);

                assert(w->drawable.bound);
                texture_bind(&w->drawable.texture, GL_TEXTURE0);

                draw_rect(w->face, type->mvp, dglPos, size);
                w->stencil_damaged = false;
            }
        }
        w_id = vector_getPrev(paints, &index);
    }

    glDisable(GL_STENCIL_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
}

void windowlist_updateShadow(session_t* ps, Vector* paints) {
    size_t index;
    win_id* w_id = vector_getLast(paints, &index);
    while(w_id != NULL) {
        struct _win* w = swiss_get(&ps->win_list, *w_id);
        if(win_viewable(w) && ps->redirected) {
            Vector2 size = {{w->widthb, w->heightb}};

            // @HACK: For legacy reasons we assume the shadow is damaged if the
            // size of the window has changed. we should move over to manually
            // damaging it when we change size
            if(w->shadow_damaged || !vec2_eq(&size, &w->shadow_cache.wSize)) {
                win_calc_shadow(ps, w);
            }
        }
        w_id = vector_getPrev(paints, &index);
    }
}

void windowlist_updateBlur(session_t* ps, Vector* paints) {
    size_t index;
    win_id* w_id = vector_getLast(paints, &index);
    while(w_id != NULL) {
        struct _win* w = swiss_get(&ps->win_list, *w_id);
        if(win_viewable(w) && ps->redirected) {
            Vector2 size = {{w->widthb, w->heightb}};

            if (w->blur_background && (!w->solid || ps->o.blur_background_frame)) {
                if(w->glx_blur_cache.damaged == true) {
                    win_calculate_blur(&ps->psglx->blur, ps, w);
                    w->glx_blur_cache.damaged = false;
                }
            }
        }
        w_id = vector_getPrev(paints, &index);
    }
}
