#include "windowlist.h"

#include "profiler/zone.h"

#include "assets/shader.h"
#include "assets/assets.h"

#include "systems/blur.h"

#include "shaders/shaderinfo.h"

#include "textureeffects.h"

#include "window.h"
#include "renderutil.h"

DECLARE_ZONE(paint_backgrounds);
DECLARE_ZONE(paint_tints);
DECLARE_ZONE(paint_windows);
DECLARE_ZONE(paint_transparents);

DECLARE_ZONE(paint_window);

DECLARE_ZONE(paint_debug);
DECLARE_ZONE(paint_debugFaders);
DECLARE_ZONE(paint_debugProps);

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
            struct TexturedComponent* textured = swiss_getComponent(&ps->win_list, COMPONENT_TEXTURED, *w_id);

            Vector2 glPos = X11_rectpos_to_gl(ps, &physical->position, &physical->size);

            if (swiss_hasComponent(&ps->win_list, COMPONENT_BLUR, *w_id)) {
                struct glx_blur_cache* blur = swiss_getComponent(&ps->win_list, COMPONENT_BLUR, *w_id);
                Vector3 dglPos = vec3_from_vec2(&glPos, z->z + 0.000001);

                {
                    struct shader_program* shader = assets_load("bgblit.shader");
                    if(shader->shader_type_info != &bgblit_info) {
                        printf_errf("Shader was not a bgblit shader\n");
                        return;
                    }
                    struct BgBlit* shader_type = shader->shader_type;
                    shader_set_future_uniform_bool(shader_type->flip, blur->texture[0].flipped);
                    shader_set_future_uniform_float(shader_type->opacity, (float)1.0);
                    shader_set_future_uniform_sampler(shader_type->tex_scr, 0);
                    shader_set_future_uniform_sampler(shader_type->win_tex, 1);

                    shader_use(shader);

                    texture_bind(&blur->texture[0], GL_TEXTURE0);
                    texture_bind(&textured->texture, GL_TEXTURE1);

                    draw_rect(shaped->face, shader_type->mvp, dglPos, physical->size);
                }
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

    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glBlendEquationSeparate(GL_FUNC_ADD, GL_MAX);

    struct face* face = assets_load("window.face");

    size_t index;
    win_id* w_id = vector_getLast(transparent, &index);
    while(w_id != NULL) {
        struct _win* w = swiss_getComponent(&ps->win_list, COMPONENT_MUD, *w_id);
        struct ShapedComponent* shaped = swiss_getComponent(&ps->win_list, COMPONENT_SHAPED, *w_id);
        struct PhysicalComponent* physical = swiss_getComponent(&ps->win_list, COMPONENT_PHYSICAL, *w_id);
        struct ZComponent* z = swiss_getComponent(&ps->win_list, COMPONENT_Z, *w_id);
        Vector2 glPos = X11_rectpos_to_gl(ps, &physical->position, &physical->size);

        // Background
        if(swiss_hasComponent(&ps->win_list, COMPONENT_BLUR, *w_id)) {
            struct glx_blur_cache* blur = swiss_getComponent(&ps->win_list, COMPONENT_BLUR, *w_id);
            Vector3 dglPos = vec3_from_vec2(&glPos, z->z + 0.00001);

            struct shader_program* passthough_program = assets_load("passthough.shader");
            if(passthough_program->shader_type_info != &passthough_info) {
                printf_errf("Shader was not a passthough shader\n");
                return;
            }

            struct BgOpacityComponent* bgOpacity = swiss_godComponent(&ps->win_list, COMPONENT_BGOPACITY, *w_id);

            struct Passthough* passthough_type = passthough_program->shader_type;
            shader_set_future_uniform_bool(passthough_type->flip, blur->texture[0].flipped);
            shader_set_future_uniform_float(passthough_type->opacity, bgOpacity != NULL ? bgOpacity->opacity/100.0 : 1.0);
            shader_set_future_uniform_sampler(passthough_type->tex_scr, 0);

            shader_use(passthough_program);

            texture_bind(&blur->texture[0], GL_TEXTURE0);

            /* Vector4 color = {{1.0, 1.0, 1.0, 1.0}}; */
            /* draw_colored_rect(shaped->face, &dglPos, &physical->size, &color); */
            draw_rect(shaped->face, passthough_type->mvp, dglPos, physical->size);
        }

        struct OpacityComponent* opacity = swiss_godComponent(&ps->win_list, COMPONENT_OPACITY, *w_id);

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

    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
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

    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
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
            draw_rect(shaped->face, global_type->mvp, winpos, physical->size);
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
        // @IMPROVE: windows that don't overlap can still contribute to the background
        // an example is shadow
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

void windowlist_drawDebug(Swiss* em, session_t* ps) {
    zone_enter(&ZONE_paint_debug);
    Vector2 scale = {{1, 1}};

    char buffer[128];

    for_components(it, em,
            COMPONENT_DEBUGGED, COMPONENT_PHYSICAL, CQ_END) {
        struct DebuggedComponent* debug = swiss_getComponent(em, COMPONENT_DEBUGGED, it.id);
        struct PhysicalComponent* physical = swiss_getComponent(em, COMPONENT_PHYSICAL, it.id);

        debug->pen = X11_rectpos_to_gl(ps, &physical->position, &physical->size);
    }

    zone_enter(&ZONE_paint_debugFaders);
    {
        struct face* face = assets_load("window.face");
        for_components(it, em,
                COMPONENT_DEBUGGED, COMPONENT_FADES_OPACITY, COMPONENT_OPACITY, CQ_END) {
            struct DebuggedComponent* debug = swiss_getComponent(em, COMPONENT_DEBUGGED, it.id);
            // @HACK @CLEANUP: we need a better way to debugdraw components that
            // might not be there. Maybe query if they are there and add some
            // custom panel or something?
            struct FadesOpacityComponent* fo = swiss_getComponent(em, COMPONENT_FADES_OPACITY, it.id);

            Vector2 barSize = {{200, 25}};
            Vector4 fgColor = {{0.0, 0.4, 0.5, 1.0}};
            Vector4 bgColor = {{.1, .1, .1, .4}};
            for(size_t i = fo->fade.head; i != fo->fade.tail; ) {
                // Increment before the body to skip head and process tail
                i = (i+1) % FADE_KEYFRAMES;

                struct FadeKeyframe* keyframe = &fo->fade.keyframes[i];

                double x = keyframe->time / keyframe->duration;

                Vector3 pos3 = vec3_from_vec2(&debug->pen, 1.0);
                draw_colored_rect(face, &pos3, &barSize, &bgColor);
                Vector3 filledPos3 = pos3;
                filledPos3.x += 5;
                filledPos3.y += 5;
                Vector2 filledSize = barSize;
                filledSize.x -= 10;
                filledSize.y -= 10;
                filledSize.x *= x;
                draw_colored_rect(face, &filledPos3, &filledSize, &fgColor);

                debug->pen.y += 10;
                debug->pen.x += 10;

                snprintf(buffer, 128, "slot %zu Target: %f", i, keyframe->target);
                text_draw(&debug_font, buffer, &debug->pen, &scale);

                debug->pen.x -= 10;
                debug->pen.y += barSize.y - 10;
            }
        }
    }

    zone_enter(&ZONE_paint_debugProps);
    for_components(it, em,
            COMPONENT_DEBUGGED, COMPONENT_FADES_OPACITY, COMPONENT_OPACITY, CQ_END) {
        struct DebuggedComponent* debug = swiss_getComponent(em, COMPONENT_DEBUGGED, it.id);
        struct OpacityComponent* opacity = swiss_getComponent(em, COMPONENT_OPACITY, it.id);
        snprintf(buffer, 128, "Opacity : %f", opacity->opacity);

        Vector2 size = {{0}};
        text_size(&debug_font, buffer, &scale, &size);
        debug->pen.y += size.y;

        text_draw(&debug_font, buffer, &debug->pen, &scale);
    }
    zone_leave(&ZONE_paint_debugProps);

    // Dim {{{
    {
        struct face* face = assets_load("window.face");
        for_components(it, em,
                COMPONENT_DEBUGGED, COMPONENT_FADES_DIM, COMPONENT_DIM, CQ_END) {
            struct DebuggedComponent* debug = swiss_getComponent(em, COMPONENT_DEBUGGED, it.id);
            // @HACK @CLEANUP: we need a better way to debugdraw components that
            // might not be there. Maybe query if they are there and add some
            // custom panel or something?
            struct FadesDimComponent* fo = swiss_getComponent(em, COMPONENT_FADES_DIM, it.id);

            Vector2 barSize = {{200, 25}};
            Vector4 fgColor = {{0.0, 0.4, 0.5, 1.0}};
            Vector4 bgColor = {{.1, .1, .1, .4}};
            for(size_t i = fo->fade.head; i != fo->fade.tail; ) {
                // Increment before the body to skip head and process tail
                i = (i+1) % FADE_KEYFRAMES;

                struct FadeKeyframe* keyframe = &fo->fade.keyframes[i];

                double x = keyframe->time / keyframe->duration;

                Vector3 pos3 = vec3_from_vec2(&debug->pen, 1.0);
                draw_colored_rect(face, &pos3, &barSize, &bgColor);
                Vector3 filledPos3 = pos3;
                filledPos3.x += 5;
                filledPos3.y += 5;
                Vector2 filledSize = barSize;
                filledSize.x -= 10;
                filledSize.y -= 10;
                filledSize.x *= x;
                draw_colored_rect(face, &filledPos3, &filledSize, &fgColor);

                debug->pen.y += 10;
                debug->pen.x += 10;

                snprintf(buffer, 128, "slot %zu Target: %f", i, keyframe->target);
                text_draw(&debug_font, buffer, &debug->pen, &scale);

                debug->pen.x -= 10;
                debug->pen.y += barSize.y - 10;
            }
        }
    }

    zone_enter(&ZONE_paint_debugProps);
    for_components(it, em,
            COMPONENT_DEBUGGED, COMPONENT_FADES_DIM, COMPONENT_DIM, CQ_END) {
        struct DebuggedComponent* debug = swiss_getComponent(em, COMPONENT_DEBUGGED, it.id);
        struct DimComponent* dim = swiss_getComponent(em, COMPONENT_DIM, it.id);
        snprintf(buffer, 128, "Dim : %f", dim->dim);

        Vector2 size = {{0}};
        text_size(&debug_font, buffer, &scale, &size);
        debug->pen.y += size.y;

        text_draw(&debug_font, buffer, &debug->pen, &scale);
    }
    zone_leave(&ZONE_paint_debugProps);
    // }}}
    zone_leave(&ZONE_paint_debugFaders);

    for_components(it, em,
            COMPONENT_DEBUGGED, COMPONENT_PHYSICAL, CQ_END) {
        struct DebuggedComponent* debug = swiss_getComponent(em, COMPONENT_DEBUGGED, it.id);
        struct PhysicalComponent* physical = swiss_getComponent(em, COMPONENT_PHYSICAL, it.id);
        Vector2 winPos = X11_rectpos_to_gl(ps, &physical->position, &physical->size);
        debug->pen = (Vector2){{winPos.x, winPos.y + physical->size.y - 20}};
    }


    zone_enter(&ZONE_paint_debugProps);
    for_components(it, em,
            COMPONENT_DEBUGGED, COMPONENT_PHYSICAL, CQ_END) {
        struct DebuggedComponent* debug = swiss_getComponent(em, COMPONENT_DEBUGGED, it.id);
        struct PhysicalComponent* physical = swiss_getComponent(em, COMPONENT_PHYSICAL, it.id);

        snprintf(buffer, 128, "Size : %fx%f", physical->size.x, physical->size.y);

        Vector2 size = {{0}};
        text_size(&debug_font, buffer, &scale, &size);
        debug->pen.y -= size.y;

        text_draw(&debug_font, buffer, &debug->pen, &scale);
    }
    zone_leave(&ZONE_paint_debugProps);

    zone_enter(&ZONE_paint_debugProps);
    for_components(it, em,
            COMPONENT_DEBUGGED, COMPONENT_STATEFUL, CQ_END) {
        struct DebuggedComponent* debug = swiss_getComponent(em, COMPONENT_DEBUGGED, it.id);
        struct StatefulComponent* state = swiss_getComponent(em, COMPONENT_STATEFUL, it.id);

        snprintf(buffer, 128, "State: %s", StateNames[state->state]);

        Vector2 size = {{0}};
        text_size(&debug_font, buffer, &scale, &size);
        debug->pen.y -= size.y;

        text_draw(&debug_font, buffer, &debug->pen, &scale);
    }
    zone_leave(&ZONE_paint_debugProps);

    zone_enter(&ZONE_paint_debugProps);
    for_components(it, em,
            COMPONENT_DEBUGGED, COMPONENT_TEXTURED, CQ_END) {
        struct DebuggedComponent* debug = swiss_getComponent(em, COMPONENT_DEBUGGED, it.id);
        struct TexturedComponent* textured = swiss_getComponent(&ps->win_list, COMPONENT_TEXTURED, it.id);

        snprintf(buffer, 128, "Texture Size : %fx%f", textured->texture.size.x, textured->texture.size.y);

        Vector2 size = {{0}};
        text_size(&debug_font, buffer, &scale, &size);
        debug->pen.y -= size.y;

        text_draw(&debug_font, buffer, &debug->pen, &scale);
    }
    zone_leave(&ZONE_paint_debugProps);

    zone_enter(&ZONE_paint_debugProps);
    for_components(it, em,
            COMPONENT_DEBUGGED, COMPONENT_BLUR, CQ_END) {
        struct DebuggedComponent* debug = swiss_getComponent(em, COMPONENT_DEBUGGED, it.id);
        struct glx_blur_cache* blur = swiss_getComponent(em, COMPONENT_BLUR, it.id);

        snprintf(buffer, 128, "Blur Size : %fx%f", blur->texture[0].size.x, blur->texture[0].size.y);

        Vector2 size = {{0}};
        text_size(&debug_font, buffer, &scale, &size);
        debug->pen.y -= size.y;

        text_draw(&debug_font, buffer, &debug->pen, &scale);
    }
    zone_leave(&ZONE_paint_debugProps);

    zone_enter(&ZONE_paint_debugProps);
    for_components(it, em,
            COMPONENT_DEBUGGED, COMPONENT_BLUR, CQ_END) {
        struct DebuggedComponent* debug = swiss_getComponent(em, COMPONENT_DEBUGGED, it.id);
        struct _win* w = swiss_getComponent(em, COMPONENT_MUD, it.id);

        snprintf(buffer, 128, "Class General : %s", w->class_general);

        Vector2 size = {{0}};
        text_size(&debug_font, buffer, &scale, &size);
        debug->pen.y -= size.y;

        text_draw(&debug_font, buffer, &debug->pen, &scale);
    }
    zone_leave(&ZONE_paint_debugProps);
    zone_leave(&ZONE_paint_debug);
}

void windowlist_drawShadow(session_t* ps) {
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);

    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glBlendEquationSeparate(GL_FUNC_ADD, GL_MAX);

    struct face* face = assets_load("window.face");

    for_components(it, &ps->win_list,
            COMPONENT_PHYSICAL, COMPONENT_SHADOW, CQ_END) {
        struct ShapedComponent* shaped = swiss_getComponent(&ps->win_list, COMPONENT_SHAPED, it.id);
        struct PhysicalComponent* physical = swiss_getComponent(&ps->win_list, COMPONENT_PHYSICAL, it.id);
        struct ZComponent* z = swiss_getComponent(&ps->win_list, COMPONENT_Z, it.id);
        struct OpacityComponent* opacity = swiss_godComponent(&ps->win_list, COMPONENT_OPACITY, it.id);
        Vector2 glPos = X11_rectpos_to_gl(ps, &physical->position, &physical->size);

        struct glx_shadow_cache* shadow = swiss_getComponent(&ps->win_list, COMPONENT_SHADOW, it.id);

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
            Vector2 rpos = {{glPos.x, glPos.y}};
            vec2_sub(&rpos, &shadow->border);
            Vector3 tdrpos = vec3_from_vec2(&rpos, z->z);
            Vector2 rsize = shadow->texture.size;

            draw_rect(face, shader_type->mvp, tdrpos, rsize);
        }
    }

    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glDisable(GL_DEPTH_TEST);
}
