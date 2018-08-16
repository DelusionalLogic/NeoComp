#define _GNU_SOURCE
#include "window.h"

#include "vmath.h"
#include "logging.h"
#include "bezier.h"
#include "windowlist.h"
#include "blur.h"
#include "assets/assets.h"
#include "profiler/zone.h"
#include "assets/shader.h"
#include "shaders/shaderinfo.h"
#include "xtexture.h"
#include "textureeffects.h"
#include "renderutil.h"
#include "shadow.h"

DECLARE_ZONE(update_window);

bool win_overlap(const win* w1, const win* w2) {
    const Vector2 w1lpos = {{
        w1->a.x, w1->a.y,
    }};
    const Vector2 w1rpos = {{
        w1->a.x + w1->widthb, w1->a.y + w1->heightb,
    }};
    const Vector2 w2lpos = {{
        w2->a.x, w2->a.y,
    }};
    const Vector2 w2rpos = {{
        w2->a.x + w2->widthb, w2->a.y + w2->heightb,
    }};
    // Horizontal collision
    if (w1lpos.x > w2rpos.x || w2lpos.x > w1rpos.x)
        return false;

    // Vertical collision
    if (w1lpos.y > w2rpos.y || w2lpos.y > w1rpos.y)
        return false;

    return true;
}

bool win_covers(win* w) {
    return w->solid
        && w->fullscreen
        && !w->unredir_if_possible_excluded;
}

static Vector2 X11_rectpos_to_gl(session_t *ps, const Vector2* xpos, const Vector2* size) {
    Vector2 glpos = {{
        xpos->x, ps->root_height - xpos->y - size->y
    }};
    return glpos;
}

void fade_init(struct Fading* fade, double value) {
    fade->head = 0;
    fade->tail = 0;
    fade->keyframes[0].target = value;
    fade->keyframes[0].time = 0;
    fade->keyframes[0].duration = -1;

    fade->value = value;
}

void fade_keyframe(struct Fading* fade, double opacity, double duration) {
    // Fast path for skipping fading
    if(duration == 0) {
        fade->head = 0;
        fade->tail = 0;
        fade->keyframes[0].target = opacity;
        fade->keyframes[0].time = 0;
        fade->keyframes[0].duration = -1;
        return;
    }

    size_t nextIndex = (fade->tail + 1) % FADE_KEYFRAMES;
    if(nextIndex == fade->head) {
        printf("Warning: Shoving something off the opacity animation\n");
        fade->head = (fade->head + 1) % FADE_KEYFRAMES;
        //The head has nothing to be blended into.
        fade->keyframes[fade->head].duration = -1;
        fade->keyframes[fade->head].time = 0;
    }

    struct FadeKeyframe* keyframe = &fade->keyframes[nextIndex];
    keyframe->target = opacity;
    keyframe->duration = duration;
    keyframe->time = 0;
    keyframe->ignore = true;
    fade->tail = nextIndex;
}

bool fade_done(struct Fading* fade) {
    return fade->tail == fade->head;
}

void win_update(session_t* ps, win* w, double dt) {
    Vector2 size = {{w->widthb, w->heightb}};

    if(!vec2_eq(&w->stencil.size, &size)) {
        renderbuffer_resize(&w->stencil, &size);
        w->stencil_damaged = true;
    }

    zone_enter(&ZONE_update_window);

    zone_leave(&ZONE_update_window);
}

static void win_drawcontents(session_t* ps, win* w, float z) {
    glx_mark(ps, 0, true);

    glEnable(GL_BLEND);

    // This is all weird, but X Render is using premultiplied ARGB format, and
    // we need to use those things to correct it. Thanks to derhass for help.
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    /* glColor4f(opacity, opacity, opacity, opacity); */

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);

    struct shader_program* global_program = assets_load("global.shader");
    if(global_program->shader_type_info != &global_info) {
        printf_errf("Shader was not a global shader");
        // @INCOMPLETE: Make sure the config is correct
        return;
    }

    struct Global* global_type = global_program->shader_type;

    shader_set_future_uniform_bool(global_type->invert, w->invert_color);
    shader_set_future_uniform_bool(global_type->flip, w->drawable.texture.flipped);
    shader_set_future_uniform_float(global_type->opacity, (float)(w->opacity / 100.0));
    shader_set_future_uniform_sampler(global_type->tex_scr, 0);

    // Dimming the window if needed
    if (w->dim) {
        double dim_opacity = ps->o.inactive_dim;
        if (!ps->o.inactive_dim_fixed)
            dim_opacity *= w->opacity / 100.0;
        shader_set_future_uniform_float(global_type->dim, dim_opacity);
    }

    shader_use(global_program);

    // Bind texture
    texture_bind(&w->drawable.texture, GL_TEXTURE0);

#ifdef DEBUG_GLX
    printf_dbgf("(): Draw: %d, %d, %d, %d -> %d, %d (%d, %d) z %d\n", x, y, width, height, dx, dy, ptex->width, ptex->height, z);
#endif

    // Painting
    {
        Vector2 rectPos = {{w->a.x, w->a.y}};
        Vector2 rectSize = {{w->widthb, w->heightb}};
        Vector2 glRectPos = X11_rectpos_to_gl(ps, &rectPos, &rectSize);
        Vector3 winpos = vec3_from_vec2(&glRectPos, z);

#ifdef DEBUG_GLX
        printf_dbgf("(): Rect %f, %f, %f, %f\n", relpos.x, relpos.y, scale.x, scale.y);
#endif

        /* Vector4 color = {{0.0, 1.0, 0.4, 1.0}}; */
        /* draw_colored_rect(w->face, &winpos, &rectSize, &color); */
        draw_rect(w->face, global_type->mvp, winpos, rectSize);
    }

    glx_mark(ps, 0, false);
}

static void win_draw_debug(session_t* ps, win* w, float z) {
    win_id wid = swiss_indexOfPointer(&ps->win_list, COMPONENT_MUD, w);
    struct face* face = assets_load("window.face");
    Vector2 scale = {{1, 1}};

    glDisable(GL_DEPTH_TEST);
    Vector2 winPos;
    Vector2 pen;
    {
        Vector2 xPen = {{w->a.x, w->a.y}};
        Vector2 size = {{w->widthb, w->heightb}};
        winPos = X11_rectpos_to_gl(ps, &xPen, &size);
        pen = winPos;
    }

    {
        // @HACK @CLEANUP: we need a better way to debugdraw components that
        // might not be there. Maybe query if they are there and add some
        // custom panel or something?
        struct FadesOpacityComponent* fo = swiss_getComponent(&ps->win_list, COMPONENT_FADES_OPACITY, wid);
        Vector2 barSize = {{200, 25}};
        Vector4 fgColor = {{0.0, 0.4, 0.5, 1.0}};
        Vector4 bgColor = {{.1, .1, .1, .4}};
        for(size_t i = fo->fade.head; i != fo->fade.tail; ) {
            // Increment before the body to skip head and process tail
            i = (i+1) % FADE_KEYFRAMES;

            struct FadeKeyframe* keyframe = &fo->fade.keyframes[i];

            double x = keyframe->time / keyframe->duration;

            Vector3 pos3 = vec3_from_vec2(&pen, 1.0);
            draw_colored_rect(face, &pos3, &barSize, &bgColor);
            Vector3 filledPos3 = pos3;
            filledPos3.x += 5;
            filledPos3.y += 5;
            Vector2 filledSize = barSize;
            filledSize.x -= 10;
            filledSize.y -= 10;
            filledSize.x *= x;
            draw_colored_rect(face, &filledPos3, &filledSize, &fgColor);

            pen.y += 10;
            pen.x += 10;

            char* text;
            asprintf(&text, "slot %zu Target: %f", i, keyframe->target);
            text_draw(&debug_font, text, &pen, &scale);
            free(text);

            pen.x -= 10;
            pen.y += barSize.y - 10;
        }

    }

    {
        char* text;
        asprintf(&text, "Opacity : %f", w->opacity);

        Vector2 size = {{0}};
        text_size(&debug_font, text, &scale, &size);
        pen.y += size.y;

        text_draw(&debug_font, text, &pen, &scale);

        free(text);
    }

    {
        pen.x = winPos.x;
        pen.y = winPos.y + w->heightb - 20;
    }

    {
        char* text;
        asprintf(&text, "State: %s", StateNames[w->state]);

        Vector2 size = {{0}};
        text_size(&debug_font, text, &scale, &size);
        pen.y -= size.y;

        text_draw(&debug_font, text, &pen, &scale);

        free(text);
    }

    {
        char* text;
        asprintf(&text, "blur-background: %d", w->blur_background);

        Vector2 size = {{0}};
        text_size(&debug_font, text, &scale, &size);
        pen.y -= size.y;

        text_draw(&debug_font, text, &pen, &scale);

        free(text);
    }

    {
        struct FadesOpacityComponent* fo = swiss_getComponent(&ps->win_list, COMPONENT_FADES_OPACITY, wid);
        char* text;
        asprintf(&text, "fade-status: %d", fade_done(&fo->fade));

        Vector2 size = {{0}};
        text_size(&debug_font, text, &scale, &size);
        pen.y -= size.y;

        text_draw(&debug_font, text, &pen, &scale);

        free(text);
    }

    {
        char* text;
        asprintf(&text, "leader: %#010lx", w->leader);

        Vector2 size = {{0}};
        text_size(&debug_font, text, &scale, &size);
        pen.y -= size.y;

        text_draw(&debug_font, text, &pen, &scale);

        free(text);
    }

    {
        char* text;
        asprintf(&text, "focused: %d", w->focused);

        Vector2 size = {{0}};
        text_size(&debug_font, text, &scale, &size);
        pen.y -= size.y;

        text_draw(&debug_font, text, &pen, &scale);

        free(text);
    }

    {
        char* text;
        asprintf(&text, "depth: %d", w->drawable.xtexture.depth);

        Vector2 size = {{0}};
        text_size(&debug_font, text, &scale, &size);
        pen.y -= size.y;

        text_draw(&debug_font, text, &pen, &scale);

        free(text);
    }

    {
        char* text;
        asprintf(&text, "verts: %zu", w->face->vertex_buffer.size / 3);

        Vector2 size = {{0}};
        text_size(&debug_font, text, &scale, &size);
        pen.y -= size.y;

        text_draw(&debug_font, text, &pen, &scale);

        free(text);
    }

    {
        char* text;
        asprintf(&text, "z: %f", w->z);

        Vector2 size = {{0}};
        text_size(&debug_font, text, &scale, &size);
        pen.y -= size.y;

        text_draw(&debug_font, text, &pen, &scale);

        free(text);
    }

    glEnable(GL_DEPTH_TEST);
}

void win_draw(session_t* ps, win* w, float z) {
    Vector2 pos = {{w->a.x, w->a.y}};
    Vector2 size = {{w->widthb, w->heightb}};
    Vector2 glPos = X11_rectpos_to_gl(ps, &pos, &size);

    // Blur the backbuffer behind the window to make transparent areas blurred.
    // @PERFORMANCE: We are also blurring things that are opaque.. Are we?
    if (w->blur_background && (!w->solid || ps->o.blur_background_frame)) {
        Vector3 dglPos = vec3_from_vec2(&glPos, z - 0.00001);

        glEnable(GL_BLEND);
        glDepthMask(GL_FALSE);
        draw_tex(w->face, &w->glx_blur_cache.texture[0], &dglPos, &size);
    }

    win_drawcontents(ps, w, z);

    win_draw_debug(ps, w, z);
}

void win_postdraw(session_t* ps, win* w, float z) {
    Vector2 pos = {{w->a.x, w->a.y}};
    Vector2 size = {{w->widthb, w->heightb}};
    Vector2 glPos = X11_rectpos_to_gl(ps, &pos, &size);

    // Painting shadow
    if (w->shadow) {
        win_paint_shadow(ps, w, &glPos, &size, w->z);
    }
}

bool wd_init(struct WindowDrawable* drawable, struct X11Context* context, Window wid) {
    assert(drawable != NULL);

    XWindowAttributes attribs;
    XGetWindowAttributes(context->display, wid, &attribs);

    drawable->wid = wid;
    drawable->fbconfig = xorgContext_selectConfig(context, XVisualIDFromVisual(attribs.visual));

    return xtexture_init(&drawable->xtexture, context);
}

bool wd_bind(struct WindowDrawable* drawable) {
    assert(drawable != NULL);

    Pixmap pixmap = XCompositeNameWindowPixmap(drawable->context->display, drawable->wid);
    if(pixmap == 0) {
        printf_errf("Failed getting window pixmap");
        return false;
    }

    return xtexture_bind(&drawable->xtexture, drawable->fbconfig, pixmap);
}

bool wd_unbind(struct WindowDrawable* drawable) {
    assert(drawable != NULL);
    xtexture_unbind(&drawable->xtexture);
    return true;
}

void wd_delete(struct WindowDrawable* drawable) {
    assert(drawable != NULL);
    // In debug mode we want to crash if we do this.
    assert(!drawable->bound);
    if(drawable->bound) {
        wd_unbind(drawable);
    }
    texture_delete(&drawable->texture);
}
