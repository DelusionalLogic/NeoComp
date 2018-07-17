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
DECLARE_ZONE(update_fade);

static bool win_viewable(win* w) {
    return w->state == STATE_DEACTIVATING || w->state == STATE_ACTIVATING
        || w->state == STATE_ACTIVE || w->state == STATE_INACTIVE
        || w->state == STATE_HIDING || w->state == STATE_DESTROYING;
}

bool win_overlap(win* w1, win* w2) {
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

bool win_calculate_blur(struct blur* blur, session_t* ps, win* w) {
    Vector2 pos = {{w->a.x, w->a.y}};
    Vector2 size = {{w->widthb, w->heightb}};

    // Read destination pixels into a texture
    struct Texture* tex = &w->glx_blur_cache.texture[1];

    Vector2 glpos = X11_rectpos_to_gl(ps, &pos, &size);
    /* texture_read_from(tex, 0, GL_BACK, &glpos, &size); */

    glEnable(GL_BLEND);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_SCISSOR_TEST);

    framebuffer_resetTarget(&blur->fbo);
    framebuffer_targetRenderBuffer_stencil(&blur->fbo, &w->glx_blur_cache.stencil);
    framebuffer_targetTexture(&blur->fbo, tex);
    framebuffer_bind(&blur->fbo);

    glDepthMask(GL_TRUE);

    glClearColor(1.0, 0.0, 1.0, 0.0);

    glEnable(GL_DEPTH_TEST);

    glClearDepth(0.0);
    glDepthFunc(GL_GREATER);

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glViewport(0, 0, size.x, size.y);
    Matrix old_view = view;
    view = mat4_orthogonal(glpos.x, glpos.x + size.x, glpos.y, glpos.y + size.y, -1, 1);

    float z = 0;
    windowlist_drawoverlap(ps, w->next_trans, w, &z);

    Vector2 root_size = {{ps->root_width, ps->root_height}};

    struct shader_program* global_program = assets_load("passthough.shader");
    if(global_program->shader_type_info != &passthough_info) {
        printf_errf("Shader was not a passthough shader\n");
        return false;
    }

    struct face* face = assets_load("window.face");
    draw_tex(face, &ps->root_texture.texture, &VEC3_ZERO, &root_size);

    view = old_view;

    glDisable(GL_BLEND);

    int level = ps->o.blur_level;

    struct TextureBlurData blurData = {
        .buffer = &blur->fbo,
        .swap = &w->glx_blur_cache.texture[0],
    };
    // Do the blur
    if(!texture_blur(&blurData, tex, level, false)) {
        printf_errf("Failed blurring the background texture");
        return false;
    }

    // Flip the blur back into texture[0] to clip to the stencil
    framebuffer_resetTarget(&blur->fbo);
    framebuffer_targetTexture(&blur->fbo, &w->glx_blur_cache.texture[0]);
    framebuffer_targetRenderBuffer_stencil(&blur->fbo, &w->stencil);
    if(framebuffer_bind(&blur->fbo) != 0) {
        printf("Failed binding framebuffer to clip blur\n");
        return false;
    }

    old_view = view;
    view = mat4_orthogonal(0, w->glx_blur_cache.texture[0].size.x, 0, w->glx_blur_cache.texture[0].size.y, -1, 1);
    glViewport(0, 0, w->glx_blur_cache.texture[0].size.x, w->glx_blur_cache.texture[0].size.y);

    glEnable(GL_BLEND);
    glClearColor(0.0, 0.0, 0.0, 0.0);
    glClear(GL_COLOR_BUFFER_BIT);

    glEnable(GL_STENCIL_TEST);

    glStencilMask(0);
    glStencilFunc(GL_EQUAL, 1, 0xFF);

    draw_tex(face, &w->glx_blur_cache.texture[1], &VEC3_ZERO, &w->glx_blur_cache.texture[0].size);

    glDisable(GL_STENCIL_TEST);
    view = old_view;

    return true;
}

void win_start_opacity(win* w, double opacity, double duration) {
    // Fast path for skipping fading
    if(duration == 0) {
        w->opacity_fade.head = 0;
        w->opacity_fade.tail = 0;
        w->opacity_fade.keyframes[0].target = opacity;
        w->opacity_fade.keyframes[0].time = 0;
        w->opacity_fade.keyframes[0].duration = -1;
    }

    size_t nextIndex = (w->opacity_fade.tail + 1) % FADE_KEYFRAMES;
    if(nextIndex == w->opacity_fade.head) {
        printf("Warning: Shoving something off the opacity animation\n");
        w->opacity_fade.head = (w->opacity_fade.head + 1) % FADE_KEYFRAMES;
    }

    struct FadeKeyframe* keyframe = &w->opacity_fade.keyframes[nextIndex];
    keyframe->target = opacity;
    keyframe->duration = duration;
    keyframe->time = 0;
    keyframe->ignore = true;
    w->opacity_fade.tail = nextIndex;
}

bool fade_done(struct Fading* fade) {
    return fade->tail == fade->head;
}

static double calc_opacity(session_t *ps, win *w) {
    double opacity = 100.0;

    // Try obeying window type opacity firstly
    opacity = ps->o.wintype_opacity[w->window_type];
    if(opacity != 100.0)
        return opacity;

    if(w->state != STATE_INVISIBLE && w->state != STATE_HIDING) {
        long val;
        if (c2_matchd(ps, w, ps->o.opacity_rules, &w->cache_oparule, &val)) {
            return (double)val;
        }
    }

    // Respect inactive_opacity in some cases
    if (ps->o.inactive_opacity && false == w->focused
            && (100.0 == opacity || ps->o.inactive_opacity_override)) {
        opacity = ps->o.inactive_opacity;
    }

    // Respect active_opacity only when the window is physically focused
    if (100.0 == opacity && ps->o.active_opacity && win_is_focused_real(ps, w)) {
        opacity = ps->o.active_opacity;
    }
    return opacity;
}

void win_update(session_t* ps, win* w, double dt) {
    Vector2 size = {{w->widthb, w->heightb}};

    if(!vec2_eq(&w->stencil.size, &size)) {
        renderbuffer_resize(&w->stencil, &size);
        w->stencil_damaged = true;
    }

    if(w->focus_changed) {
        if(w->state != STATE_HIDING && w->state != STATE_INVISIBLE
                && w->state != STATE_DESTROYING && w->state != STATE_DESTROYED) {
            if(w->focused) {
                w->state = STATE_ACTIVATING;
            } else {
                w->state = STATE_DEACTIVATING;
            }
            double opacity = calc_opacity(ps, w);
            win_start_opacity(w, opacity, ps->o.opacity_fade_time);
        }
        w->focus_changed = false;
    }

    zone_enter(&ZONE_update_window);
    w->opacity_fade.value = w->opacity_fade.keyframes[w->opacity_fade.head].target;

    if(!fade_done(&w->opacity_fade)) {
        zone_enter(&ZONE_update_fade);

        // If we aren't redirected we just want to skip animations
        if(!ps->redirected) {
            struct FadeKeyframe* keyframe = &w->opacity_fade.keyframes[w->opacity_fade.tail];
            // We're done, clean out the time and set this as the head
            keyframe->time = 0.0;
            w->opacity_fade.head = w->opacity_fade.tail;
            w->opacity_fade.value = keyframe->target;
        }

        // @CLEANUP: Maybe a while loop?
        for(size_t i = w->opacity_fade.head; i != w->opacity_fade.tail; ) {
            // Increment before the body to skip head and process tail
            i = (i+1) % FADE_KEYFRAMES;

            struct FadeKeyframe* keyframe = &w->opacity_fade.keyframes[i];
            if(!keyframe->ignore){
                keyframe->time += dt;
            } else {
                keyframe->ignore = false;
            }

            double x = keyframe->time / keyframe->duration;
            if(x >= 1.0) {
                // We're done, clean out the time and set this as the head
                keyframe->time = 0.0;
                keyframe->duration = -1;
                w->opacity_fade.head = i;

                // Force the value. We are still going to blend it with stuff
                // on top of this
                w->opacity_fade.value = keyframe->target;
            } else {
                double t = bezier_getTForX(&ps->curve, x);
                w->opacity_fade.value = lerp(w->opacity_fade.value, keyframe->target, t);
            }
        }
        ps->skip_poll = true;

        // If the fade isn't done, then damage the blur of everyone above
        for (win *t = w->prev_trans; t; t = t->prev_trans) {
            if(win_overlap(w, t))
                t->glx_blur_cache.damaged = true;
        }
        zone_leave(&ZONE_update_fade);
    }

    if(fade_done(&w->opacity_fade)) {
        if(w->state == STATE_ACTIVATING) {
            w->state = STATE_ACTIVE;

            w->in_openclose = false;
        } else if(w->state == STATE_DEACTIVATING) {
            w->state = STATE_INACTIVE;
        } else if(w->state == STATE_HIDING) {
            w->damaged = false;

            w->in_openclose = false;

            free_region(ps, &w->border_size);
            if(ps->redirected)
                wd_unbind(&w->drawable);

            w->state = STATE_INVISIBLE;
        } else if(w->state == STATE_DESTROYING) {
            w->state = STATE_DESTROYED;
        }
        void (*old_callback) (session_t *ps, win *w) = w->fade_callback;
        w->fade_callback = NULL;
        if(old_callback != NULL) {
            old_callback(ps, w);
            ps->idling = false;
        }
    }
    w->opacity = w->opacity_fade.value;

    zone_leave(&ZONE_update_window);
}

static void win_drawcontents(session_t* ps, win* w, float z) {
    glx_mark(ps, w->id, true);

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

    glx_mark(ps, w->id, false);
}

static void win_draw_debug(session_t* ps, win* w, float z) {
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
        Vector2 barSize = {{200, 25}};
        Vector4 fgColor = {{0.0, 0.4, 0.5, 1.0}};
        Vector4 bgColor = {{.1, .1, .1, .4}};
        for(size_t i = w->opacity_fade.head; i != w->opacity_fade.tail; ) {
            // Increment before the body to skip head and process tail
            i = (i+1) % FADE_KEYFRAMES;

            struct FadeKeyframe* keyframe = &w->opacity_fade.keyframes[i];

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
        char* text;
        asprintf(&text, "fade-status: %d", fade_done(&w->opacity_fade));

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
        asprintf(&text, "verts: %zu", w->face->vertex_buffer_size);

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

    if(win_viewable(w)) {
        // Painting shadow
        if (w->shadow) {
            win_paint_shadow(ps, w, &glPos, &size, z + 0.00001);
        }
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
