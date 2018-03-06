#define GL_GLEXT_PROTOTYPES
#include "blur.h"

#include "assets/assets.h"
#include "assets/shader.h"
#include "shaders/shaderinfo.h"
#include "renderutil.h"
#include "textureeffects.h"
#include "framebuffer.h"

#include <stdio.h>

static inline GLuint
generate_texture(GLenum tex_tgt, const Vector2* size) {
    GLuint tex = 0;

    glGenTextures(1, &tex);
    if (!tex)
        return 0;

    glBindTexture(tex_tgt, tex);
    glTexParameteri(tex_tgt, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(tex_tgt, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(tex_tgt, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(tex_tgt, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(tex_tgt, 0, GL_RGB, size->x, size->y, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);

    glBindTexture(tex_tgt, 0);

    return tex;
}

void blur_init(struct blur* blur) {
    glGenVertexArrays(1, &blur->array);
    glBindVertexArray(blur->array);

    blur->face = assets_load("window.face");
    if(blur->face == NULL) {
        printf("Failed loading window drawing face\n");
    }
}

static Vector2 X11_rectpos_to_gl(session_t *ps, const Vector2* xpos, const Vector2* size) {
    Vector2 glpos = {{
        xpos->x, ps->root_height - xpos->y - size->y
    }};
    return glpos;
}

bool blur_backbuffer(struct blur* blur, session_t* ps, const Vector2* pos,
        const Vector2* size, float z, GLfloat factor_center,
        XserverRegion reg_tgt, const reg_data_t *pcache_reg,
        glx_blur_cache_t* pbc){
#ifdef DEBUG_GLX
    printf_dbgf("(): %f, %f, %f, %f\n", pos->x, pos->y, size->x, size->y);
#endif
    const bool have_scissors = glIsEnabled(GL_SCISSOR_TEST);
    const bool have_stencil = glIsEnabled(GL_STENCIL_TEST);

    // Make sure the blur cache is initialized. This is a noop if it's already
    // initialized
    if(blur_cache_init(pbc, size) != 0) {
        printf_errf("(): Failed to initializing cache");
        return false;
    }

    struct Texture* tex_scr = &pbc->texture[0];
    // Read destination pixels into a texture

    Vector2 glpos = X11_rectpos_to_gl(ps, pos, size);
    texture_read_from(tex_scr, 0, GL_BACK, &glpos, size);

    // Texture scaling factor
    Vector2 pixeluv = {{1.0f, 1.0f}};
    vec2_div(&pixeluv, size);
    Vector2 halfpixel = pixeluv;
    vec2_idiv(&halfpixel, 2);

    // Disable the options. We will restore later
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_SCISSOR_TEST);

    int level = ps->o.blur_level;

    // Do the blur
    if(!texture_blur(&pbc->fbo, tex_scr, level)) {
        printf_errf("Failed blurring the background texture");

        if (have_scissors)
            glEnable(GL_SCISSOR_TEST);
        if (have_stencil)
            glEnable(GL_STENCIL_TEST);
        return false;
    }

    glViewport(0, 0, ps->root_width, ps->root_height);

    // Lets just make sure we write this back into the stenctil buffer
    if (have_scissors)
        glEnable(GL_SCISSOR_TEST);
    if (have_stencil)
        glEnable(GL_STENCIL_TEST);

    // Render back to the backbuffer
    struct shader_program* passthough_program = assets_load("passthough.shader");
    if(passthough_program->shader_type_info != &passthough_info) {
        printf_errf("Shader was not a passthough shader");
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glBindTexture(GL_TEXTURE_2D, 0);
        if (have_scissors)
            glEnable(GL_SCISSOR_TEST);
        if (have_stencil)
            glEnable(GL_STENCIL_TEST);
        return false;
    }

    struct Passthough* passthough_type = passthough_program->shader_type;
    shader_use(passthough_program);

    shader_set_uniform_bool(passthough_type->flip, false);

    // Bind the final blur texture
    texture_bind(tex_scr, GL_TEXTURE0);

    // Bind the default framebuffer and draw back to the backbuffer to actually
    // render the frame to screen
    {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        static const GLenum DRAWBUFS[2] = { GL_BACK_LEFT };
        glDrawBuffers(1, DRAWBUFS);

        // Reenable those configs saved at the start
        if (have_scissors)
            glEnable(GL_SCISSOR_TEST);
        if (have_stencil)
            glEnable(GL_STENCIL_TEST);
    }

    //Final render
    {
        Vector2 root_size = {{ps->root_width, ps->root_height}};
        Vector2 pixeluv = {{1.0f, 1.0f}};
        vec2_div(&pixeluv, &root_size);

        XserverRegion reg_new = None;
        XRectangle rec_all = { .x = pos->x, .y = pos->y, .width = size->x, .height = size->y };
        XRectangle *rects = &rec_all;
        int nrects = 1;

        if (ps->o.glx_no_stencil && reg_tgt) {
            if (pcache_reg) {
                rects = pcache_reg->rects;
                nrects = pcache_reg->nrects;
            }
            else {
                reg_new = XFixesCreateRegion(ps->dpy, &rec_all, 1);
                XFixesIntersectRegion(ps->dpy, reg_new, reg_new, reg_tgt);

                nrects = 0;
                rects = XFixesFetchRegion(ps->dpy, reg_new, &nrects);
            }
        }

        for (int ri = 0; ri < nrects; ++ri) {
            XRectangle crect;
            rect_crop(&crect, &rects[ri], &rec_all);

            Vector2 rectPos = {{crect.x, crect.y}};
            Vector2 rectSize = {{crect.width, crect.height}};
            Vector2 glRectPos = X11_rectpos_to_gl(ps, &rectPos, &rectSize);

            if (!crect.width || !crect.height)
                continue;

            Vector2 scale = pixeluv;
            vec2_mul(&scale, &rectSize);

            Vector2 relpos = pixeluv;
            vec2_mul(&relpos, &glRectPos);

#ifdef DEBUG_GLX
            printf_dbgf("glpos: %f %f, relpos %f %f scale %f %f\n",
                    glRectPos.x, glRectPos.y, relpos.x, relpos.y, scale.x,
                    scale.y);
#endif

            draw_rect(ps->psglx->blur.face, passthough_type->mvp, relpos, scale);
        }

        if (rects && rects != &rec_all && !(pcache_reg && pcache_reg->rects == rects))
            cxfree(rects);
        free_region(ps, &reg_new);
    }

    // Restore the default rendering context
    glUseProgram(0);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    if (have_scissors)
        glEnable(GL_SCISSOR_TEST);
    if (have_stencil)
        glEnable(GL_STENCIL_TEST);

    return true;
}

void blur_destroy(struct blur* blur) {
    glDeleteVertexArrays(1, &blur->array);
    free(blur);
}

int blur_cache_init(glx_blur_cache_t* cache, const Vector2* size) {
    if(!vec2_eq(size, &cache->size)) {
        if(texture_initialized(&cache->texture[0]))
            texture_delete(&cache->texture[0]);

        if(texture_initialized(&cache->texture[1]))
            texture_delete(&cache->texture[1]);
    }

    // Generate textures if needed
    if(!texture_initialized(&cache->texture[0])) {
        texture_init(&cache->texture[0], GL_TEXTURE_2D, size);
        cache->textures[0] = cache->texture[0].gl_texture;
    }
    /* if(cache->textures[0] == 0) */
    /*     cache->textures[0] = generate_texture(GL_TEXTURE_2D, size); */

    if(cache->textures[0] == 0) {
        printf("Failed allocating texture for cache\n");
        return 1;
    }

    if(!texture_initialized(&cache->texture[1])) {
        texture_init(&cache->texture[1], GL_TEXTURE_2D, size);
        cache->textures[1] = cache->texture[1].gl_texture;
    }
    /* if(cache->textures[1] == 0) */
    /*     cache->textures[1] = generate_texture(GL_TEXTURE_2D, size); */

    if(cache->textures[1] == 0) {
        printf("Failed allocating texture for cache\n");
        return 1;
    }

    // Generate FBO if needed
    if(!framebuffer_initialized(&cache->fbo)) {
        if(!framebuffer_init(&cache->fbo)) {
            printf("Failed allocating framebuffer for cache\n");
            return 1;
        }
    }

    // Set the size
    cache->size = *size;

    return 0;
}
