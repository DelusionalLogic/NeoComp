#include "window.h"

#include "vmath.h"
#include "blur.h"
#include "assets/assets.h"
#include "assets/shader.h"
#include "shaders/shaderinfo.h"
#include "textureeffects.h"

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
        && (!w->has_frame || !w->frame_opacity)
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
    if(blur_cache_init(&w->glx_blur_cache, &size) != 0) {
        printf_errf("(): Failed to initializing cache");
        return false;
    }

    if(w->glx_blur_cache.damaged) {
        struct Texture* tex = &w->glx_blur_cache.texture[0];
        // Read destination pixels into a texture

        Vector2 glpos = X11_rectpos_to_gl(ps, &pos, &size);
        /* texture_read_from(tex, 0, GL_BACK, &glpos, &size); */

        framebuffer_resetTarget(&blur->fbo);
        framebuffer_targetTexture(&blur->fbo, tex);
        framebuffer_bind(&blur->fbo);

        glDisable(GL_STENCIL_TEST);
        glDisable(GL_SCISSOR_TEST);

        glClearColor(1.0, 0.0, 0.0, 1.0);
        glClear(GL_COLOR_BUFFER_BIT);

        glViewport(0, 0, size.x, size.y);

        struct shader_program* global_program = assets_load("passthough.shader");
        if(global_program->shader_type_info != &passthough_info) {
            printf_errf("Shader was not a passthough shader\n");
            return false;
        }

        struct face* face = assets_load("window.face");

        Vector2 root_size = size;
        Vector2 pixeluv = {{1.0f, 1.0f}};
        vec2_div(&pixeluv, &tex->size);

        for(win* t = w->next_trans; t != NULL; t = t->next_trans) {
            Vector2 tpos = {{t->a.x, t->a.y}};
            Vector2 tsize = {{t->widthb, t->heightb}};
            Vector2 tglpos = X11_rectpos_to_gl(ps, &tpos, &tsize);
            vec2_sub(&tglpos, &glpos);

            Vector2 scale = pixeluv;
            vec2_mul(&scale, &tsize);

            Vector2 relpos = pixeluv;
            vec2_mul(&relpos, &tglpos);
            printf("SHADOW_DRAW %f %f x %f %f\n", relpos.x, relpos.y, scale.x, scale.y);

            draw_tex(ps, face, &t->glx_blur_cache.texture[0], &relpos, &scale);
            struct Texture ttt = {
                .target = GL_TEXTURE_2D,
                .gl_texture = t->paint.ptex->texture,
                /* .gl_texture = ps->root_tile_paint.ptex->texture, */
            };
            draw_tex(ps, face, &ttt, &relpos, &scale);
        }

        // Texture scaling factor
        Vector2 halfpixel = pixeluv;
        vec2_idiv(&halfpixel, 2);

        // Disable the options. We will restore later
        glDisable(GL_STENCIL_TEST);
        glDisable(GL_SCISSOR_TEST);

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

bool wd_bind(struct WindowDrawable* drawable, Display* display, Window wid) {
    drawable->wid = wid;
    drawable->pixmap = XCompositeNameWindowPixmap(display, wid);
    if(drawable->pixmap == 0) {
        printf_errf("Failed getting window pixmap");
        return false;
    }
    return true;
}
void wd_delete(struct WindowDrawable* drawable) {
}
