#include "window.h"

#include "vmath.h"
#include "blur.h"
#include "assets/assets.h"
#include "assets/shader.h"
#include "shaders/shaderinfo.h"
#include "xtexture.h"
#include "textureeffects.h"
#include "renderutil.h"

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

    if(w->glx_blur_cache.damaged) {
        struct Texture* tex = &w->glx_blur_cache.texture[0];
        // Read destination pixels into a texture

        Vector2 glpos = X11_rectpos_to_gl(ps, &pos, &size);
        /* texture_read_from(tex, 0, GL_BACK, &glpos, &size); */

        glEnable(GL_STENCIL_TEST);
        glEnable(GL_BLEND);
        glDisable(GL_SCISSOR_TEST);

        framebuffer_resetTarget(&blur->fbo);
        framebuffer_targetRenderBuffer_stencil(&blur->fbo, &w->glx_blur_cache.stencil);
        framebuffer_targetTexture(&blur->fbo, tex);
        framebuffer_bind(&blur->fbo);

        glClearColor(1.0, 1.0, 0.0, 1.0);

        glStencilMask(0xFF);
        glClearStencil(0);
        glStencilFunc(GL_EQUAL, 0, 0xFF);
        glStencilOp(GL_KEEP, GL_KEEP, GL_INCR);

        glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

        glViewport(0, 0, size.x, size.y);
        Matrix old_view = view;
        view = mat4_orthogonal(glpos.x, glpos.x + size.x, glpos.y, glpos.y + size.y, -1, 1);

        struct shader_program* global_program = assets_load("passthough.shader");
        if(global_program->shader_type_info != &passthough_info) {
            printf_errf("Shader was not a passthough shader\n");
            return false;
        }

        struct face* face = assets_load("window.face");

        for(win* t = w->next_trans; t != NULL; t = t->next_trans) {
            Vector2 tpos = {{t->a.x, t->a.y}};
            Vector2 tsize = {{t->widthb, t->heightb}};
            Vector2 tglpos = X11_rectpos_to_gl(ps, &tpos, &tsize);

            {
                Vector3 pos = vec3_from_vec2(&tglpos, 0.0);
                draw_tex(ps, face, &t->drawable.texture, &pos, &tsize);
            }
        }

        Vector2 root_size = {{ps->root_width, ps->root_height}};
        draw_tex(ps, face, &ps->root_texture.texture, &VEC3_ZERO, &root_size);

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
        w->glx_blur_cache.damaged = false;
    }
    return true;
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
