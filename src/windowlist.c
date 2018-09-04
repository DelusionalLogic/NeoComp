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

Vector2 X11_rectpos_to_gl(session_t *ps, const Vector2* xpos, const Vector2* size) {
    Vector2 glpos = {{
        xpos->x, ps->root_height - xpos->y - size->y
    }};
    return glpos;
}

void windowlist_drawBackground(session_t* ps, Vector* opaque) {
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);

    {
        size_t index;
        win_id* w_id = vector_getFirst(opaque, &index);
        while(w_id != NULL) {
            struct _win* w = swiss_getComponent(&ps->win_list, COMPONENT_MUD, *w_id);
            struct PhysicalComponent* physical = swiss_getComponent(&ps->win_list, COMPONENT_PHYSICAL, *w_id);
            struct ZComponent* z = swiss_getComponent(&ps->win_list, COMPONENT_Z, *w_id);

            Vector2 glPos = X11_rectpos_to_gl(ps, &physical->position, &physical->size);

            if (swiss_hasComponent(&ps->win_list, COMPONENT_BLUR, *w_id)) {
                struct glx_blur_cache* blur = swiss_getComponent(&ps->win_list, COMPONENT_BLUR, *w_id);
                Vector3 dglPos = vec3_from_vec2(&glPos, z->z + 0.000001);

                draw_tex(w->face, &blur->texture[0], &dglPos, &physical->size);
            }

            w_id = vector_getNext(opaque, &index);
        }
    }

    glDepthMask(GL_FALSE);
    glDisable(GL_DEPTH_TEST);
}

void windowlist_drawTransparent(session_t* ps, Vector* transparent) {
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);

    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    size_t index;
    win_id* w_id = vector_getLast(transparent, &index);
    while(w_id != NULL) {
        struct _win* w = swiss_getComponent(&ps->win_list, COMPONENT_MUD, *w_id);
        struct PhysicalComponent* physical = swiss_getComponent(&ps->win_list, COMPONENT_PHYSICAL, *w_id);
        struct ZComponent* z = swiss_getComponent(&ps->win_list, COMPONENT_Z, *w_id);
        struct OpacityComponent* opacity = swiss_getComponent(&ps->win_list, COMPONENT_OPACITY, *w_id);
        struct TexturedComponent* textured = swiss_getComponent(&ps->win_list, COMPONENT_TEXTURED, *w_id);
        Vector2 glPos = X11_rectpos_to_gl(ps, &physical->position, &physical->size);

        // Background
        {

            if (w->blur_background && (!w->solid || ps->o.blur_background_frame)) {
                struct glx_blur_cache* blur = swiss_getComponent(&ps->win_list, COMPONENT_BLUR, *w_id);
                Vector3 dglPos = vec3_from_vec2(&glPos, z->z + 0.00001);

                struct shader_program* passthough_program = assets_load("passthough.shader");
                if(passthough_program->shader_type_info != &passthough_info) {
                    printf_errf("Shader was not a passthough shader\n");
                    return;
                }
                struct Passthough* passthough_type = passthough_program->shader_type;
                shader_set_future_uniform_bool(passthough_type->flip, blur->texture[0].flipped);
                shader_set_future_uniform_float(passthough_type->opacity, opacity->opacity/100.0);
                shader_set_future_uniform_sampler(passthough_type->tex_scr, 0);

                shader_use(passthough_program);

                texture_bind(&blur->texture[0], GL_TEXTURE0);

                /* Vector4 color = {{opacity->opacity/100, opacity->opacity/100, opacity->opacity/100, opacity->opacity/100}}; */
                /* draw_colored_rect(w->face, &dglPos, &physical->size, &color); */
                draw_rect(w->face, passthough_type->mvp, dglPos, physical->size);
            }
        }

        // Tint
        if(swiss_getComponent(&ps->win_list, COMPONENT_TINT, *w_id)) {
            struct TintComponent* tint = swiss_getComponent(&ps->win_list, COMPONENT_TINT, *w_id);
            struct shader_program* program = assets_load("tint.shader");
            if(program->shader_type_info != &colored_info) {
                printf_errf("Shader was not a colored shader");
                return;
            }
            struct Colored* shader_type = program->shader_type;

            shader_set_future_uniform_vec2(shader_type->viewport, &(Vector2){{ps->root_width, ps->root_height}});
            shader_use(program);

            {
                double opac = opacity->opacity / 100.0;
                shader_set_uniform_float(shader_type->opacity, tint->color.w * opac);
                Vector3 color = tint->color.rgb;
                vec3_imul(&color, opac);
                shader_set_uniform_vec3(shader_type->color, &color);
            }

            {
                Vector2 glRectPos = X11_rectpos_to_gl(ps, &physical->position, &physical->size);
                Vector3 winpos = vec3_from_vec2(&glRectPos, z->z);

                /* Vector4 color = {{0.0, 1.0, 0.4, 1.0}}; */
                /* draw_colored_rect(w->face, &winpos, &textured->texture.size, &color); */
                draw_rect(w->face, shader_type->mvp, winpos, physical->size);
            }
        }

        // Content
        {
            struct shader_program* global_program = assets_load("global.shader");
            if(global_program->shader_type_info != &global_info) {
                printf_errf("Shader was not a global shader");
                // @INCOMPLETE: Make sure the config is correct
                return;
            }

            struct Global* global_type = global_program->shader_type;

            shader_set_future_uniform_sampler(global_type->tex_scr, 0);

            shader_use(global_program);
            zone_enter_extra(&ZONE_paint_window, "%s", w->name);

            shader_set_uniform_bool(global_type->invert, w->invert_color);
            shader_set_uniform_bool(global_type->flip, textured->texture.flipped);
            shader_set_uniform_float(global_type->opacity, (float)(opacity->opacity / 100.0));

            // Bind texture
            texture_bind(&textured->texture, GL_TEXTURE0);

            {
                Vector2 glRectPos = X11_rectpos_to_gl(ps, &physical->position, &textured->texture.size);
                Vector3 winpos = vec3_from_vec2(&glRectPos, z->z);

                /* Vector4 color = {{0.0, 1.0, 0.4, opacity->opacity/100}}; */
                /* draw_colored_rect(w->face, &winpos, &textured->texture.size, &color); */
                draw_rect(w->face, global_type->mvp, winpos, textured->texture.size);
            }

            zone_leave(&ZONE_paint_window);
        }

        w_id = vector_getPrev(transparent, &index);
    }

    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glDisable(GL_DEPTH_TEST);
}

void windowlist_drawTint(session_t* ps) {
    glEnable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);

    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    struct shader_program* program = assets_load("tint.shader");
    if(program->shader_type_info != &colored_info) {
        printf_errf("Shader was not a colored shader");
        glDepthMask(GL_TRUE);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        return;
    }
    struct Colored* shader_type = program->shader_type;

    shader_set_future_uniform_vec2(shader_type->viewport, &(Vector2){{ps->root_width, ps->root_height}});
    shader_use(program);

    for_components(it, &ps->win_list,
            COMPONENT_MUD, COMPONENT_TINT, COMPONENT_PHYSICAL, COMPONENT_Z, CQ_END) {
        struct _win* w = swiss_getComponent(&ps->win_list, COMPONENT_MUD, it.id);
        struct TintComponent* tint = swiss_getComponent(&ps->win_list, COMPONENT_TINT, it.id);
        struct PhysicalComponent* physical = swiss_getComponent(&ps->win_list, COMPONENT_PHYSICAL, it.id);
        struct ZComponent* z = swiss_getComponent(&ps->win_list, COMPONENT_Z, it.id);

        shader_set_uniform_float(shader_type->opacity, tint->color.w);
        shader_set_uniform_vec3(shader_type->color, &tint->color.rgb);

        {
            Vector2 glRectPos = X11_rectpos_to_gl(ps, &physical->position, &physical->size);
            Vector3 winpos = vec3_from_vec2(&glRectPos, z->z);

            /* Vector4 color = {{0.0, 1.0, 0.4, 1.0}}; */
            /* draw_colored_rect(w->face, &winpos, &textured->texture.size, &color); */
            draw_rect(w->face, shader_type->mvp, winpos, physical->size);
        }
    }

    glDepthMask(GL_TRUE);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
}

void windowlist_draw(session_t* ps, Vector* order) {
    glx_mark(ps, 0, true);

    glEnable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);

    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    struct shader_program* global_program = assets_load("global.shader");
    if(global_program->shader_type_info != &global_info) {
        printf_errf("Shader was not a global shader");
        glDepthMask(GL_TRUE);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
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

    glDepthMask(GL_TRUE);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);

    glx_mark(ps, 0, false);
}

size_t binaryZSearch(Swiss* em, const Vector* candidates, double needle) {
    size_t len = vector_size(candidates);
    if(len == 0)
        return 0;

    size_t low = 0;
    size_t high = len-1;

    {
        win_id id = *(win_id*)vector_get(candidates, high);
        struct ZComponent* z = swiss_getComponent(em, COMPONENT_Z, id);
        if(z->z < needle) {
            return len;
        }
    }

    while(low < high) {
        size_t mid = (low + high) / 2;

        win_id id = *(win_id*)vector_get(candidates, mid);
        struct ZComponent* z = swiss_getComponent(em, COMPONENT_Z, id);

        if(z->z > needle) {
            high = mid;
            if(mid == 0)
                return 0;
        } else {
            low  = mid + 1;
            if(low == len)
                return len;
        }
    }

    return low;
}

void windowlist_findbehind(Swiss* win_list, const Vector* windows, const win_id overlap, Vector* overlaps) {
    size_t len = vector_size(windows);
    if(len == 0)
        return;

    struct ZComponent* z = swiss_getComponent(win_list, COMPONENT_Z, overlap);

    size_t index = binaryZSearch(win_list, windows, z->z);
    if(index >= vector_size(windows))
        return;

    win_id* wid = vector_get(windows, index);
    while(wid != NULL) {
        if(win_overlap(win_list, overlap, *wid))
            vector_putBack(overlaps, wid);
        wid = vector_getNext(windows, &index);
    }
}

void windowlist_updateBlur(session_t* ps) {
    Vector to_blur;
    vector_init(&to_blur, sizeof(win_id), ps->win_list.size);
    for_components(it, &ps->win_list,
            COMPONENT_MUD, COMPONENT_BLUR, COMPONENT_BLUR_DAMAGED, COMPONENT_Z, COMPONENT_PHYSICAL, CQ_END) {
        vector_putBack(&to_blur, &it.id);
    }
    vector_qsort(&to_blur, window_zcmp, &ps->win_list);

    Vector opaque_renderable;
    vector_init(&opaque_renderable, sizeof(win_id), ps->win_list.size);
    for_components(it, &ps->win_list,
            COMPONENT_MUD, COMPONENT_TEXTURED, COMPONENT_Z, CQ_NOT, COMPONENT_OPACITY, COMPONENT_PHYSICAL, CQ_END) {
        vector_putBack(&opaque_renderable, &it.id);
    }
    vector_qsort(&opaque_renderable, window_zcmp, &ps->win_list);

    Vector transparent_renderable;
    vector_init(&transparent_renderable, sizeof(win_id), ps->win_list.size);
    for_components(it, &ps->win_list,
            COMPONENT_MUD, COMPONENT_TEXTURED, COMPONENT_Z, COMPONENT_OPACITY, COMPONENT_PHYSICAL, CQ_END) {
        vector_putBack(&transparent_renderable, &it.id);
    }
    vector_qsort(&transparent_renderable, window_zcmp, &ps->win_list);

    struct blur* cache = &ps->psglx->blur;

    framebuffer_resetTarget(&cache->fbo);
    framebuffer_bind(&cache->fbo);

    struct face* face = assets_load("window.face");

    glDisable(GL_STENCIL_TEST);
    glDisable(GL_SCISSOR_TEST);

    // Blurring is a strange process, because every window depends on the blurs
    // behind it. Therefore we render them individually, starting from the
    // back.
    size_t index;
    win_id* w_id = vector_getLast(&to_blur, &index);
    while(w_id != NULL) {
        struct PhysicalComponent* physical = swiss_getComponent(&ps->win_list, COMPONENT_PHYSICAL, *w_id);
        struct glx_blur_cache* blur = swiss_getComponent(&ps->win_list, COMPONENT_BLUR, *w_id);

        Vector2 glpos = X11_rectpos_to_gl(ps, &physical->position, &physical->size);

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

        // Find the drawables behind this one
        Vector opaque_behind;
        vector_init(&opaque_behind, sizeof(win_id), 16);
        windowlist_findbehind(&ps->win_list, &opaque_renderable, *w_id, &opaque_behind);
        Vector transparent_behind;
        vector_init(&transparent_behind, sizeof(win_id), 16);
        windowlist_findbehind(&ps->win_list, &transparent_renderable, *w_id, &transparent_behind);

        windowlist_drawBackground(ps, &opaque_behind);
        windowlist_draw(ps, &opaque_behind);

        // Draw root
        glEnable(GL_DEPTH_TEST);
        Vector2 root_size = {{ps->root_width, ps->root_height}};
        draw_tex(face, &ps->root_texture.texture, &(Vector3){{0, 0, 0.99999}}, &root_size);
        glDisable(GL_DEPTH_TEST);

        windowlist_drawTransparent(ps, &transparent_behind);

        vector_kill(&transparent_behind);
        vector_kill(&opaque_behind);

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

        w_id = vector_getPrev(&to_blur, &index);
    }

    swiss_resetComponent(&ps->win_list, COMPONENT_BLUR_DAMAGED);
}
