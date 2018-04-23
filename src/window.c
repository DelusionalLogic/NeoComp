#include "window.h"

#include "vmath.h"
#include "windowlist.h"
#include "blur.h"
#include "assets/assets.h"
#include "assets/shader.h"
#include "shaders/shaderinfo.h"
#include "xtexture.h"
#include "textureeffects.h"
#include "renderutil.h"
#include "shadow.h"

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
    const bool have_scissors = glIsEnabled(GL_SCISSOR_TEST);
    const bool have_stencil = glIsEnabled(GL_STENCIL_TEST);

    Vector2 pos = {{w->a.x, w->a.y}};
    Vector2 size = {{w->widthb, w->heightb}};

    struct Texture* tex = &w->glx_blur_cache.texture[0];
    // Read destination pixels into a texture

    Vector2 glpos = X11_rectpos_to_gl(ps, &pos, &size);
    /* texture_read_from(tex, 0, GL_BACK, &glpos, &size); */

    glEnable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);

    framebuffer_resetTarget(&blur->fbo);
    framebuffer_targetRenderBuffer_stencil(&blur->fbo, &w->glx_blur_cache.stencil);
    framebuffer_targetTexture(&blur->fbo, tex);
    framebuffer_bind(&blur->fbo);

    glClearColor(0.0, 1.0, 0.0, 1.0);

    glClearDepth(0.0);
    glDepthFunc(GL_GREATER);

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glViewport(0, 0, size.x, size.y);
    Matrix old_view = view;
    view = mat4_orthogonal(glpos.x, glpos.x + size.x, glpos.y, glpos.y + size.y, -1, 1);

    /* glEnable(GL_DEPTH_TEST); */

    float z = 0;
    windowlist_draw(ps, w->next_trans, &z);

    Vector2 root_size = {{ps->root_width, ps->root_height}};

    struct shader_program* global_program = assets_load("passthough.shader");
    if(global_program->shader_type_info != &passthough_info) {
        printf_errf("Shader was not a passthough shader\n");
        return false;
    }
    struct face* face = assets_load("window.face");

    glEnable(GL_DEPTH_TEST);

    draw_tex(ps, face, &ps->root_texture.texture, &VEC3_ZERO, &root_size);

    glDisable(GL_DEPTH_TEST);

    /* glDisable(GL_DEPTH_TEST); */

    view = old_view;

    // Disable the options. We will restore later
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);

    int level = ps->o.blur_level;

    struct TextureBlurData blurData = {
        .buffer = &blur->fbo,
        .swap = &w->glx_blur_cache.texture[1],
    };
    // Do the blur
    if(!texture_blur(&blurData, tex, level, false)) {
        printf_errf("Failed blurring the background texture");

        if (have_scissors)
            glEnable(GL_SCISSOR_TEST);
        if (have_stencil)
            glEnable(GL_STENCIL_TEST);
        return false;
    }
    return true;
}

void win_update(session_t* ps, win* w) {
    if(w->a.map_state != IsViewable)
        return;

    Vector2 pos = {{w->a.x, w->a.y}};
    Vector2 size = {{w->widthb, w->heightb}};

    if (w->blur_background && (!w->solid || ps->o.blur_background_frame)) {
        if(w->glx_blur_cache.damaged == true) {
            win_calculate_blur(&ps->psglx->blur, ps, w);
            w->glx_blur_cache.damaged = false;
        }
    }

    if(!vec2_eq(&size, &w->shadow_cache.wSize)) {
        win_calc_shadow(ps, w);
    }

}

static double
get_opacity_percent(win *w) {
  return ((double) w->opacity) / OPAQUE;
}

static void win_drawcontents(session_t* ps, win* w, float z) {
    glx_mark(ps, w->id, true);

    const double dopacity = get_opacity_percent(w);

    glEnable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);

    // This is all weird, but X Render is using premultiplied ARGB format, and
    // we need to use those things to correct it. Thanks to derhass for help.
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    /* glColor4f(opacity, opacity, opacity, opacity); */

    struct shader_program* global_program = assets_load("global.shader");
    if(global_program->shader_type_info != &global_info) {
        printf_errf("Shader was not a global shader");
        // @INCOMPLETE: Make sure the config is correct
        return;
    }

    struct Global* global_type = global_program->shader_type;
    shader_use(global_program);

    // Bind texture
    texture_bind(&w->drawable.texture, GL_TEXTURE0);

    shader_set_uniform_float(global_type->invert, w->invert_color);
    shader_set_uniform_float(global_type->flip, w->drawable.texture.flipped);
    shader_set_uniform_float(global_type->opacity, dopacity);
    shader_set_uniform_sampler(global_type->tex_scr, 0);

    // Dimming the window if needed
    if (w->dim) {
        double dim_opacity = ps->o.inactive_dim;
        if (!ps->o.inactive_dim_fixed)
            dim_opacity *= get_opacity_percent(w);
        shader_set_uniform_float(global_type->dim, dim_opacity);
    } else {
        shader_set_uniform_float(global_type->dim, 0.0);
    }

#ifdef DEBUG_GLX
    printf_dbgf("(): Draw: %d, %d, %d, %d -> %d, %d (%d, %d) z %d\n", x, y, width, height, dx, dy, ptex->width, ptex->height, z);
#endif

    struct face* face = assets_load("window.face");

    // Painting
    {
        Vector2 rectPos = {{w->a.x, w->a.y}};
        Vector2 rectSize = {{w->widthb, w->heightb}};
        Vector2 glRectPos = X11_rectpos_to_gl(ps, &rectPos, &rectSize);
        Vector3 winpos = vec3_from_vec2(&glRectPos, z);

#ifdef DEBUG_GLX
        printf_dbgf("(): Rect %f, %f, %f, %f\n", relpos.x, relpos.y, scale.x, scale.y);
#endif

        draw_rect(face, global_type->mvp, winpos, rectSize);
    }

    // Cleanup
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);

    glx_mark(ps, w->id, false);
}

void win_draw(session_t* ps, win* w, float z) {
    struct face* face = assets_load("window.face");

    Vector2 pos = {{w->a.x, w->a.y}};
    Vector2 size = {{w->widthb, w->heightb}};
    Vector2 glPos = X11_rectpos_to_gl(ps, &pos, &size);
    // Blur the backbuffer behind the window to make transparent areas blurred.
    // @PERFORMANCE: We are also blurring things that are opaque
    if (w->blur_background && (!w->solid || ps->o.blur_background_frame)) {
        Vector3 dglPos = vec3_from_vec2(&glPos, z - 0.00001);

        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);

        draw_tex(ps, face, &w->glx_blur_cache.texture[0], &dglPos, &size);

        glDepthMask(GL_TRUE);
        glDisable(GL_DEPTH_TEST);
    }

    win_drawcontents(ps, w, z);
}

void win_postdraw(session_t* ps, win* w, float z) {
    if(w->a.map_state != IsViewable)
        return;

    Vector2 pos = {{w->a.x, w->a.y}};
    Vector2 size = {{w->widthb, w->heightb}};
    Vector2 glPos = X11_rectpos_to_gl(ps, &pos, &size);

    // Painting shadow
    if (w->shadow) {
        win_paint_shadow(ps, w, &glPos, &size, z + 0.00001);
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
    if(drawable->bound) {
        wd_unbind(drawable);
    }
    texture_delete(&drawable->texture);
}
