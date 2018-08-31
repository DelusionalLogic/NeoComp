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

static Vector2 X11_rectpos_to_gl(session_t *ps, const Vector2* xpos, const Vector2* size) {
    Vector2 glpos = {{
        xpos->x, ps->root_height - xpos->y - size->y
    }};
    return glpos;
}

static bool win_viewable(win* w) {
    return w->state == STATE_DEACTIVATING || w->state == STATE_ACTIVATING
        || w->state == STATE_ACTIVE || w->state == STATE_INACTIVE
        || w->state == STATE_HIDING || w->state == STATE_DESTROYING;
}

void windowlist_drawBackground(session_t* ps, Vector* order) {
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);

    size_t index;
    win_id* w_id = vector_getFirst(order, &index);
    while(w_id != NULL) {
        struct _win* w = swiss_getComponent(&ps->win_list, COMPONENT_MUD, *w_id);
        struct PhysicalComponent* physical = swiss_getComponent(&ps->win_list, COMPONENT_PHYSICAL, *w_id);
        struct ZComponent* z = swiss_getComponent(&ps->win_list, COMPONENT_Z, *w_id);

        Vector2 glPos = X11_rectpos_to_gl(ps, &physical->position, &physical->size);

        if (w->blur_background && (!w->solid || ps->o.blur_background_frame)) {
            struct glx_blur_cache* blur = swiss_getComponent(&ps->win_list, COMPONENT_BLUR, *w_id);
            Vector3 dglPos = vec3_from_vec2(&glPos, z->z + 0.000001);

            draw_tex(w->face, &blur->texture[0], &dglPos, &physical->size);
        }

        w_id = vector_getNext(order, &index);
    }
}

void windowlist_draw(session_t* ps, Vector* order) {
    glx_mark(ps, 0, true);

    {
        glEnable(GL_BLEND);
        glEnable(GL_DEPTH_TEST);

        // This is all weird, but X Render is using premultiplied ARGB format, and
        // we need to use those things to correct it. Thanks to derhass for help.
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

        glDepthMask(GL_TRUE);

        struct shader_program* global_program = assets_load("global.shader");
        if(global_program->shader_type_info != &global_info) {
            printf_errf("Shader was not a global shader");
            // @INCOMPLETE: Make sure the config is correct
            return;
        }

        struct Global* global_type = global_program->shader_type;

        shader_set_future_uniform_sampler(global_type->tex_scr, 0);

        shader_use(global_program);

        size_t index;
        win_id* w_id = vector_getFirst(order, &index);
        while(w_id != NULL) {
            struct _win* w = swiss_getComponent(&ps->win_list, COMPONENT_MUD, *w_id);
            struct TexturedComponent* textured = swiss_getComponent(&ps->win_list, COMPONENT_TEXTURED, *w_id);
            struct PhysicalComponent* physical = swiss_getComponent(&ps->win_list, COMPONENT_PHYSICAL, *w_id);
            struct ZComponent* z = swiss_getComponent(&ps->win_list, COMPONENT_Z, *w_id);

            zone_enter_extra(&ZONE_paint_window, "%s", w->name);

            shader_set_uniform_bool(global_type->invert, w->invert_color);
            shader_set_uniform_bool(global_type->flip, textured->texture.flipped);
            shader_set_uniform_float(global_type->opacity, (float)(w->opacity / 100.0));

            // Dimming the window if needed
            if (w->dim) {
                double dim_opacity = ps->o.inactive_dim;
                if (!ps->o.inactive_dim_fixed)
                    dim_opacity *= w->opacity / 100.0;
                shader_set_uniform_float(global_type->dim, dim_opacity);
            }

            // Bind texture
            texture_bind(&textured->texture, GL_TEXTURE0);

            {
                Vector2 glRectPos = X11_rectpos_to_gl(ps, &physical->position, &textured->texture.size);
                Vector3 winpos = vec3_from_vec2(&glRectPos, z->z);

                /* Vector4 color = {{0.0, 1.0, 0.4, 1.0}}; */
                /* draw_colored_rect(w->face, &winpos, &textured->texture.size, &color); */
                draw_rect(w->face, global_type->mvp, winpos, textured->texture.size);
            }

            zone_leave(&ZONE_paint_window);

            w_id = vector_getNext(order, &index);
        }
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

void windowlist_updateBlur(session_t* ps, Vector* paints) {
#if 0
    Vector to_blur;
    Vector to_render_behind;
#endif

    struct blur* cache = &ps->psglx->blur;

    framebuffer_resetTarget(&cache->fbo);
    framebuffer_bind(&cache->fbo);

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
                struct glx_blur_cache* blur = swiss_getComponent(&ps->win_list, COMPONENT_BLUR, *w_id);
                if(swiss_hasComponent(&ps->win_list, COMPONENT_BLUR_DAMAGED, *w_id)) {

                    struct Texture* tex = &blur->texture[1];

                    framebuffer_resetTarget(&cache->fbo);
                    framebuffer_targetRenderBuffer_stencil(&cache->fbo, &blur->stencil);
                    framebuffer_targetTexture(&cache->fbo, tex);
                    framebuffer_rebind(&cache->fbo);

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

                    Vector behind;
                    vector_init(&behind, sizeof(win_id), vector_size(paints) - index);
                    windowlist_findbehind(&ps->win_list, paints, w, index, &behind);

                    windowlist_drawBackground(ps, &behind);
                    windowlist_draw(ps, &behind);

                    vector_kill(&behind);

                    Vector2 root_size = {{ps->root_width, ps->root_height}};

                    draw_tex(face, &ps->root_texture.texture, &(Vector3){{0, 0, 0.99999}}, &root_size);

                    view = old_view;

                    glDisable(GL_BLEND);

                    int level = ps->o.blur_level;

                    struct TextureBlurData blurData = {
                        .depth = &blur->stencil,
                        .tex = tex,
                        .swap = &blur->texture[0],
                    };
                    // Do the blur
                    if(!texture_blur(&blurData, &cache->fbo, level, false)) {
                        printf_errf("Failed blurring the background texture\n");
                        return;
                    }

                    // Flip the blur back into texture[0] to clip to the stencil
                    framebuffer_resetTarget(&cache->fbo);
                    framebuffer_targetTexture(&cache->fbo, &blur->texture[0]);
                    if(framebuffer_rebind(&cache->fbo) != 0) {
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

                }
            }
        }

        w_id = vector_getPrev(paints, &index);
    }

    swiss_resetComponent(&ps->win_list, COMPONENT_BLUR_DAMAGED);
}
