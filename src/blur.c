#define GL_GLEXT_PROTOTYPES
#include "blur.h"

#include "assets/assets.h"
#include "assets/shader.h"
#include "shaders/shaderinfo.h"
#include "renderutil.h"
#include "window.h"
#include "textureeffects.h"
#include "framebuffer.h"

#include <stdio.h>

void blur_init(struct blur* blur) {
    glGenVertexArrays(1, &blur->array);
    glBindVertexArray(blur->array);

    // Generate FBO if needed
    if(!framebuffer_initialized(&blur->fbo)) {
        if(!framebuffer_init(&blur->fbo)) {
            printf("Failed allocating framebuffer for cache\n");
            return;
        }
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
        glx_blur_cache_t* pbc, win* w) {
    glx_mark(ps, 0xDEADBEEF, true);
#ifdef DEBUG_GLX
    printf_dbgf("(): %f, %f, %f, %f\n", pos->x, pos->y, size->x, size->y);
#endif
    const bool have_stencil = glIsEnabled(GL_STENCIL_TEST);

    struct Texture* tex_scr = &pbc->texture[0];

    glViewport(0, 0, ps->root_width, ps->root_height);

    // Lets just make sure we write this back into the stenctil buffer
    if (have_stencil)
        glEnable(GL_STENCIL_TEST);

    // Render back to the backbuffer
    struct shader_program* passthough_program = assets_load("passthough.shader");
    if(passthough_program->shader_type_info != &passthough_info) {
        printf_errf("Shader was not a passthough shader");
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glBindTexture(GL_TEXTURE_2D, 0);
        if (have_stencil)
            glEnable(GL_STENCIL_TEST);
        return false;
    }

    struct Passthough* passthough_type = passthough_program->shader_type;
    shader_use(passthough_program);

    shader_set_uniform_bool(passthough_type->flip, false);
    shader_set_uniform_float(passthough_type->opacity, 1.0);

    // Bind the final blur texture
    texture_bind(tex_scr, GL_TEXTURE0);

    // Bind the default framebuffer and draw back to the backbuffer to actually
    // render the frame to screen
    {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        static const GLenum DRAWBUFS[2] = { GL_BACK_LEFT };
        glDrawBuffers(1, DRAWBUFS);

        // Reenable those configs saved at the start
        if (have_stencil)
            glEnable(GL_STENCIL_TEST);
    }

    //Final render
    {
        Vector2 rectPos = *pos;
        Vector2 rectSize = *size;
        Vector2 glRectPos = X11_rectpos_to_gl(ps, &rectPos, &rectSize);

#ifdef DEBUG_GLX
        printf_dbgf("glpos: %f %f, relpos %f %f scale %f %f\n",
                glRectPos.x, glRectPos.y, relpos.x, relpos.y, scale.x,
                scale.y);
#endif

        {
            Vector3 pos3 = vec3_from_vec2(&glRectPos, 1);
            draw_rect(w->face, passthough_type->mvp, pos3, rectSize);
        }
    }

    // Restore the default rendering context
    glUseProgram(0);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    if (have_stencil)
        glEnable(GL_STENCIL_TEST);

    return true;
}

void blur_destroy(struct blur* blur) {
    glDeleteVertexArrays(1, &blur->array);
    free(blur);
}

bool blur_cache_resize(glx_blur_cache_t* cache, const Vector2* size) {
    assert(renderbuffer_initialized(&cache->stencil));
    assert(texture_initialized(&cache->texture[0]));
    assert(texture_initialized(&cache->texture[1]));

    cache->size = *size;
    // Start out damaged for force a redraw when we resize
    cache->damaged = true;

    renderbuffer_resize(&cache->stencil, size);
    texture_resize(&cache->texture[0], size);
    texture_resize(&cache->texture[1], size);
    return true;
}

bool blur_cache_init(glx_blur_cache_t* cache) {
    assert(!renderbuffer_initialized(&cache->stencil));
    assert(!texture_initialized(&cache->texture[0]));
    assert(!texture_initialized(&cache->texture[1]));

    if(renderbuffer_stencil_init(&cache->stencil, NULL) != 0) {
        printf("Failed allocating stencil for cache\n");
        return false;
    }

    if(texture_init(&cache->texture[0], GL_TEXTURE_2D, NULL) != 0) {
        printf("Failed allocating texture for cache\n");
        renderbuffer_delete(&cache->stencil);
        return false;
    }

    if(texture_init(&cache->texture[1], GL_TEXTURE_2D, NULL) != 0) {
        printf("Failed allocating texture for cache\n");
        renderbuffer_delete(&cache->stencil);
        texture_delete(&cache->texture[0]);
        return false;
    }

    return true;
}

void blur_cache_delete(glx_blur_cache_t* cache) {
    assert(renderbuffer_initialized(&cache->stencil));
    assert(texture_initialized(&cache->texture[0]));
    assert(texture_initialized(&cache->texture[1]));

    renderbuffer_delete(&cache->stencil);
    texture_delete(&cache->texture[0]);
    texture_delete(&cache->texture[1]);
}
