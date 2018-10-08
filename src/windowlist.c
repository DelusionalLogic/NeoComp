#include "windowlist.h"

#include "profiler/zone.h"

#include "assets/shader.h"
#include "assets/assets.h"

#include "shaders/shaderinfo.h"

#include "textureeffects.h"

#include "window.h"
#include "blur.h"
#include "renderutil.h"

DECLARE_ZONE(paint_backgrounds);
DECLARE_ZONE(paint_tints);
DECLARE_ZONE(paint_windows);
DECLARE_ZONE(paint_transparents);

DECLARE_ZONE(paint_window);

Vector2 X11_rectpos_to_gl(session_t *ps, const Vector2* xpos, const Vector2* size) {
    Vector2 glpos = {{
        xpos->x, ps->root_size.y - xpos->y - size->y
    }};
    return glpos;
}

void windowlist_drawBackground(session_t* ps, Vector* opaque) {
    zone_enter(&ZONE_paint_backgrounds);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);

    {
        size_t index;
        win_id* w_id = vector_getFirst(opaque, &index);
        while(w_id != NULL) {
            struct ShapedComponent* shaped = swiss_getComponent(&ps->win_list, COMPONENT_SHAPED, *w_id);
            struct PhysicalComponent* physical = swiss_getComponent(&ps->win_list, COMPONENT_PHYSICAL, *w_id);
            struct ZComponent* z = swiss_getComponent(&ps->win_list, COMPONENT_Z, *w_id);

            Vector2 glPos = X11_rectpos_to_gl(ps, &physical->position, &physical->size);

            if (swiss_hasComponent(&ps->win_list, COMPONENT_BLUR, *w_id)) {
                struct glx_blur_cache* blur = swiss_getComponent(&ps->win_list, COMPONENT_BLUR, *w_id);
                Vector3 dglPos = vec3_from_vec2(&glPos, z->z + 0.000001);

                draw_tex(shaped->face, &blur->texture[0], &dglPos, &physical->size);
            }

            w_id = vector_getNext(opaque, &index);
        }
    }

    glDepthMask(GL_FALSE);
    glDisable(GL_DEPTH_TEST);
    zone_leave(&ZONE_paint_backgrounds);
}

void windowlist_drawTransparent(session_t* ps, Vector* transparent) {
    zone_enter(&ZONE_paint_transparents);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);

    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBlendEquationSeparate(GL_FUNC_ADD, GL_MAX);

    size_t index;
    win_id* w_id = vector_getLast(transparent, &index);
    while(w_id != NULL) {
        struct _win* w = swiss_getComponent(&ps->win_list, COMPONENT_MUD, *w_id);
        struct ShapedComponent* shaped = swiss_getComponent(&ps->win_list, COMPONENT_SHAPED, *w_id);
        struct PhysicalComponent* physical = swiss_getComponent(&ps->win_list, COMPONENT_PHYSICAL, *w_id);
        struct ZComponent* z = swiss_getComponent(&ps->win_list, COMPONENT_Z, *w_id);
        Vector2 glPos = X11_rectpos_to_gl(ps, &physical->position, &physical->size);

        struct OpacityComponent* opacity = swiss_godComponent(&ps->win_list, COMPONENT_OPACITY, *w_id);

        // Background
        if(opacity != NULL && swiss_hasComponent(&ps->win_list, COMPONENT_BLUR, *w_id)) {
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
            draw_rect(shaped->face, passthough_type->mvp, dglPos, physical->size);
        }

        // Tint
        if(opacity != NULL && swiss_hasComponent(&ps->win_list, COMPONENT_TINT, *w_id)) {
            struct TintComponent* tint = swiss_getComponent(&ps->win_list, COMPONENT_TINT, *w_id);
            struct shader_program* program = assets_load("tint.shader");
            if(program->shader_type_info != &colored_info) {
                printf_errf("Shader was not a colored shader");
                return;
            }
            struct Colored* shader_type = program->shader_type;

            shader_set_future_uniform_vec2(shader_type->viewport, &ps->root_size);
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

                draw_rect(shaped->face, shader_type->mvp, winpos, physical->size);
            }
        }

        // Shadow
        // This renders shadows for all windows, transparent or no.
        if(swiss_hasComponent(&ps->win_list, COMPONENT_SHADOW, *w_id)) {
            struct glx_shadow_cache* shadow = swiss_getComponent(&ps->win_list, COMPONENT_SHADOW, *w_id);
            struct shader_program* program = assets_load("passthough.shader");
            if(program->shader_type_info != &passthough_info) {
                printf_errf("Shader was not a passthrough shader");
                return;
            }
            struct Passthough* shader_type = program->shader_type;

            shader_set_future_uniform_bool(shader_type->flip, shadow->effect.flipped);
            shader_set_future_uniform_sampler(shader_type->tex_scr, 0);
            if(opacity != NULL) {
                shader_set_future_uniform_float(shader_type->opacity, opacity->opacity / 100.0);
            } else {
                shader_set_future_uniform_float(shader_type->opacity, 1.0);
            }
            shader_use(program);

            texture_bind(&shadow->effect, GL_TEXTURE0);

            {
                Vector2 rpos = glPos;
                vec2_sub(&rpos, &shadow->border);
                Vector3 tdrpos = vec3_from_vec2(&rpos, z->z);
                Vector2 rsize = shadow->texture.size;

                draw_rect(shaped->face, shader_type->mvp, tdrpos, rsize);
            }
        }


        // Content
        if(opacity != NULL && swiss_hasComponent(&ps->win_list, COMPONENT_TEXTURED, *w_id)) {
            struct TexturedComponent* textured = swiss_getComponent(&ps->win_list, COMPONENT_TEXTURED, *w_id);
            struct DimComponent* dim = swiss_getComponent(&ps->win_list, COMPONENT_DIM, *w_id);
            struct shader_program* global_program = assets_load("global.shader");
            if(global_program->shader_type_info != &global_info) {
                printf_errf("Shader was not a global shader");
                // @INCOMPLETE: Make sure the config is correct
                return;
            }

            struct Global* global_type = global_program->shader_type;

            shader_set_future_uniform_sampler(global_type->tex_scr, 0);

            shader_set_future_uniform_bool(global_type->invert, w->invert_color);
            shader_set_future_uniform_bool(global_type->flip, textured->texture.flipped);
            shader_set_future_uniform_float(global_type->opacity, (float)(opacity->opacity / 100.0));
            shader_set_future_uniform_float(global_type->dim, dim->dim/100.0);

            shader_use(global_program);
            zone_enter_extra(&ZONE_paint_window, "%s", w->name);

            // Bind texture
            texture_bind(&textured->texture, GL_TEXTURE0);

            {
                Vector2 glRectPos = X11_rectpos_to_gl(ps, &physical->position, &textured->texture.size);
                Vector3 winpos = vec3_from_vec2(&glRectPos, z->z);

                /* Vector4 color = {{0.0, 1.0, 0.4, opacity->opacity/100}}; */
                /* draw_colored_rect(w->face, &winpos, &textured->texture.size, &color); */
                draw_rect(shaped->face, global_type->mvp, winpos, textured->texture.size);
            }

            zone_leave(&ZONE_paint_window);
        }

        w_id = vector_getPrev(transparent, &index);
    }

    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glDisable(GL_DEPTH_TEST);
    zone_leave(&ZONE_paint_transparents);
}

void windowlist_drawTint(session_t* ps) {
    zone_enter(&ZONE_paint_tints);
    glEnable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);

    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBlendEquationSeparate(GL_FUNC_ADD, GL_MAX);

    struct shader_program* program = assets_load("tint.shader");
    if(program->shader_type_info != &colored_info) {
        printf_errf("Shader was not a colored shader");
        glDepthMask(GL_TRUE);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        return;
    }
    struct Colored* shader_type = program->shader_type;

    shader_set_future_uniform_vec2(shader_type->viewport, &ps->root_size);
    shader_use(program);

    for_components(it, &ps->win_list,
            COMPONENT_MUD, COMPONENT_TINT, COMPONENT_PHYSICAL, COMPONENT_Z, CQ_END) {
        struct ShapedComponent* shaped = swiss_getComponent(&ps->win_list, COMPONENT_SHAPED, it.id);
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
            draw_rect(shaped->face, shader_type->mvp, winpos, physical->size);
        }
    }

    glDepthMask(GL_TRUE);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    zone_leave(&ZONE_paint_tints);
}

void windowlist_draw(session_t* ps, Vector* order) {
    zone_enter(&ZONE_paint_windows);
    glEnable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);

    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBlendEquationSeparate(GL_FUNC_ADD, GL_MAX);

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
        struct ShapedComponent* shaped = swiss_getComponent(&ps->win_list, COMPONENT_SHAPED, *w_id);
        struct PhysicalComponent* physical = swiss_getComponent(&ps->win_list, COMPONENT_PHYSICAL, *w_id);
        struct DimComponent* dim = swiss_getComponent(&ps->win_list, COMPONENT_DIM, *w_id);
        struct ZComponent* z = swiss_getComponent(&ps->win_list, COMPONENT_Z, *w_id);

        zone_enter_extra(&ZONE_paint_window, "%s", w->name);

        shader_set_uniform_bool(global_type->invert, w->invert_color);
        shader_set_uniform_bool(global_type->flip, textured->texture.flipped);
        shader_set_uniform_float(global_type->dim, dim->dim/100.0);

        // Bind texture
        texture_bind(&textured->texture, GL_TEXTURE0);

        {
            Vector2 glRectPos = X11_rectpos_to_gl(ps, &physical->position, &textured->texture.size);
            Vector3 winpos = vec3_from_vec2(&glRectPos, z->z);

            /* Vector4 color = {{0.0, 1.0, 0.4, 1.0}}; */
            /* draw_colored_rect(w->face, &winpos, &textured->texture.size, &color); */
            draw_rect(shaped->face, global_type->mvp, winpos, textured->texture.size);
        }

        zone_leave(&ZONE_paint_window);

        w_id = vector_getNext(order, &index);
    }

    glDepthMask(GL_TRUE);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    zone_leave(&ZONE_paint_windows);
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
        if(z->z <= needle) {
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
        struct ZComponent* oz = swiss_getComponent(win_list, COMPONENT_Z, *wid);
        assert(oz->z > z->z);
        if(win_overlap(win_list, overlap, *wid))
            vector_putBack(overlaps, wid);
        wid = vector_getNext(windows, &index);
    }
}

void fetchSortedWindowsWithArr(Swiss* em, Vector* result, CType* query) {
    for_componentsArr(it, em, query) {
        vector_putBack(result, &it.id);
    }
    vector_qsort(result, window_zcmp, em);
}
#define fetchSortedWindowsWith(em, result, ...) \
    fetchSortedWindowsWithArr(em, result, (CType[]){ __VA_ARGS__ })

void windowlist_updateBlur(session_t* ps) {
    Vector to_blur;
    vector_init(&to_blur, sizeof(win_id), ps->win_list.size);
    fetchSortedWindowsWith(&ps->win_list, &to_blur, 
            COMPONENT_MUD, COMPONENT_BLUR, COMPONENT_BLUR_DAMAGED, COMPONENT_Z,
            COMPONENT_PHYSICAL, CQ_END);

    Vector opaque_renderable;
    vector_init(&opaque_renderable, sizeof(win_id), ps->win_list.size);
    fetchSortedWindowsWith(&ps->win_list, &opaque_renderable, 
            COMPONENT_MUD, COMPONENT_TEXTURED, COMPONENT_Z, COMPONENT_PHYSICAL,
            CQ_NOT, COMPONENT_OPACITY, CQ_END);

    Vector shadow_renderable;
    vector_init(&shadow_renderable, sizeof(win_id), ps->win_list.size);
    fetchSortedWindowsWith(&ps->win_list, &shadow_renderable, 
            COMPONENT_MUD, COMPONENT_SHADOW, COMPONENT_Z, COMPONENT_PHYSICAL,
            CQ_NOT, COMPONENT_OPACITY, CQ_END);

    Vector transparent_renderable;
    vector_init(&transparent_renderable, sizeof(win_id), ps->win_list.size);
    // @PERFORMANCE: We should probably restrict these windows to only those
    // that could possibly do something in drawTransparent. for that we need
    // some way to merge vectors.
    fetchSortedWindowsWith(&ps->win_list, &transparent_renderable, 
            COMPONENT_MUD, COMPONENT_Z, COMPONENT_PHYSICAL,
            /* COMPONENT_OPACITY, */ CQ_END);

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

        Vector shadow_behind;
        vector_init(&shadow_behind, sizeof(win_id), 16);
        windowlist_findbehind(&ps->win_list, &shadow_renderable, *w_id, &shadow_behind);

        windowlist_drawBackground(ps, &opaque_behind);
        windowlist_draw(ps, &opaque_behind);

        // Draw root
        glEnable(GL_DEPTH_TEST);
        draw_tex(face, &ps->root_texture.texture, &(Vector3){{0, 0, 0.99999}}, &ps->root_size);
        glDisable(GL_DEPTH_TEST);

        windowlist_drawTransparent(ps, &transparent_behind);

        vector_kill(&opaque_behind);
        vector_kill(&shadow_behind);
        vector_kill(&transparent_behind);

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

    vector_kill(&transparent_renderable);
    vector_kill(&shadow_renderable);
    vector_kill(&opaque_renderable);
    vector_kill(&to_blur);

    swiss_resetComponent(&ps->win_list, COMPONENT_BLUR_DAMAGED);
}

void windowlist_drawDebug(Swiss* em, session_t* ps) {
    Vector2 pen;
    Vector2 scale = {{1, 1}};

    for_components(it, em,
            COMPONENT_PHYSICAL, COMPONENT_TEXTURED, COMPONENT_BLUR, CQ_END) {
        struct PhysicalComponent* physical = swiss_getComponent(em, COMPONENT_PHYSICAL, it.id);
        struct TexturedComponent* textured = swiss_getComponent(&ps->win_list, COMPONENT_TEXTURED, it.id);
        struct glx_blur_cache* blur = swiss_getComponent(em, COMPONENT_BLUR, it.id);

        pen = X11_rectpos_to_gl(ps, &physical->position, &physical->size);

        {
            char* text;
            asprintf(&text, "Size : %fx%f", physical->size.x, physical->size.y);

            Vector2 size = {{0}};
            text_size(&debug_font, text, &scale, &size);
            pen.y += size.y;

            text_draw(&debug_font, text, &pen, &scale);

            free(text);
        }
        {
            char* text;
            asprintf(&text, "Texture Size : %fx%f", textured->texture.size.x, textured->texture.size.y);

            Vector2 size = {{0}};
            text_size(&debug_font, text, &scale, &size);
            pen.y += size.y;

            text_draw(&debug_font, text, &pen, &scale);

            free(text);
        }
        {
            char* text;
            asprintf(&text, "Blur Size : %fx%f", blur->texture[0].size.x, blur->texture[0].size.y);

            Vector2 size = {{0}};
            text_size(&debug_font, text, &scale, &size);
            pen.y += size.y;

            text_draw(&debug_font, text, &pen, &scale);

            free(text);
        }
    }
}
