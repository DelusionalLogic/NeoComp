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
        struct _win* w = swiss_getComponent(&ps->win_list, COMPONENT_MUD, *w_id);
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

        Vector2 pos = {{w->a.x, w->a.y}};
        Vector2 size = {{w->widthb, w->heightb}};
        Vector2 glpos = X11_rectpos_to_gl(ps, &pos, &size);

        if(win_viewable(w)) {
            if (w->blur_background && (!w->solid || ps->o.blur_background_frame)) {
                if(w->glx_blur_cache.damaged == true) {

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

                    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

                    glViewport(0, 0, size.x, size.y);
                    Matrix old_view = view;
                    view = mat4_orthogonal(glpos.x, glpos.x + size.x, glpos.y, glpos.y + size.y, -1, 1);

                    Vector behind;
                    vector_init(&behind, sizeof(win_id), index);

                    windowlist_findbehind(&ps->win_list, paints, w, index, &behind);

                    float z = 0;
                    windowlist_draw(ps, &behind, &z);

                    vector_kill(&behind);

                    Vector2 root_size = {{ps->root_width, ps->root_height}};

                    struct shader_program* global_program = assets_load("passthough.shader");
                    if(global_program->shader_type_info != &passthough_info) {
                        printf_errf("Shader was not a passthough shader\n");
                        return;
                    }
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
                    framebuffer_targetRenderBuffer_stencil(&blur->fbo, &w->stencil);
                    if(framebuffer_rebind(&blur->fbo) != 0) {
                        printf("Failed binding framebuffer to clip blur\n");
                        return;
                    }

                    old_view = view;
                    view = mat4_orthogonal(0, w->glx_blur_cache.texture[0].size.x, 0, w->glx_blur_cache.texture[0].size.y, -1, 1);
                    glViewport(0, 0, w->glx_blur_cache.texture[0].size.x, w->glx_blur_cache.texture[0].size.y);

                    glClearColor(0.0, 0.0, 0.0, 0.0);
                    glClear(GL_COLOR_BUFFER_BIT);

                    glEnable(GL_STENCIL_TEST);

                    glStencilMask(0);
                    glStencilFunc(GL_EQUAL, 1, 0xFF);

                    draw_tex(face, &w->glx_blur_cache.texture[1], &VEC3_ZERO, &w->glx_blur_cache.texture[0].size);

                    glDisable(GL_STENCIL_TEST);
                    view = old_view;

                    w->glx_blur_cache.damaged = false;
                }
            }
        }

        w_id = vector_getPrev(paints, &index);
    }

}
