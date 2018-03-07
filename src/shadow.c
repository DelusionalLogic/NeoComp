#include "shadow.h"

#include "assets/assets.h"
#include "assets/shader.h"
#include "shaders/shaderinfo.h"
#include "textureeffects.h"

#include "renderutil.h"

static Vector2 X11_rectpos_to_gl(session_t *ps, const Vector2* xpos, const Vector2* size) {
    Vector2 glpos = {{
        xpos->x, ps->root_height - xpos->y - size->y
    }};
    return glpos;
}

void window_shadow(session_t* ps, win* w, const Vector2* pos, const Vector2* size) {
    Vector2 border = {{64, 64}};

    Vector2 overflowSize = border;
    vec2_imul(&overflowSize, 2);
    vec2_add(&overflowSize, size);

    struct Texture texture;
    if(texture_init(&texture, GL_TEXTURE_2D, &overflowSize) != 0) {
        printf("Couldn't create texture for shadow\n");
        return;
    }

    struct RenderBuffer buffer;
    if(renderbuffer_stencil_init(&buffer, &overflowSize) != 0) {
        printf("Couldn't create renderbuffer stencil for shadow\n");
        texture_delete(&texture);
        return;
    }

    struct Framebuffer framebuffer;
    if(!framebuffer_init(&framebuffer)) {
        printf("Couldn't create framebuffer for shadow\n");
        texture_delete(&texture);
        renderbuffer_delete(&buffer);
        return;
    }

    framebuffer_targetTexture(&framebuffer, &texture);
    framebuffer_targetRenderBuffer_stencil(&framebuffer, &buffer);
    framebuffer_bind(&framebuffer);

    glViewport(0, 0, texture.size.x, texture.size.y);

    glEnable(GL_BLEND);
    glEnable(GL_STENCIL_TEST);
	bool hadScissor = glIsEnabled(GL_SCISSOR_TEST);
    glDisable(GL_SCISSOR_TEST);

    glClearColor(0.0, 0.0, 0.0, 0.0);

    glStencilMask(0xFF);
    glClearStencil(0);
    glStencilFunc(GL_EQUAL, 0, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_INCR);

    glClear(GL_STENCIL_BUFFER_BIT | GL_COLOR_BUFFER_BIT);

    // @CLEANUP: We have to do this since the window isn't using the new nice
    // interface
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, w->paint.ptex->texture);

    struct shader_program* global_program = assets_load("shadow.shader");
    if(global_program->shader_type_info != &global_info) {
        printf_errf("Shader was not a global shader\n");
        texture_delete(&texture);
        renderbuffer_delete(&buffer);
        framebuffer_delete(&framebuffer);
        return;
    }

    struct Global* global_type = global_program->shader_type;
    shader_use(global_program);

    shader_set_uniform_float(global_type->invert, false);
    shader_set_uniform_float(global_type->flip, true);
    shader_set_uniform_float(global_type->opacity, 1.0);
    shader_set_uniform_sampler(global_type->tex_scr, 0);

    struct face* face = assets_load("window.face");

    {
        Vector2 pixeluv = {{1.0f, 1.0f}};
        vec2_div(&pixeluv, &texture.size);

        Vector2 scale = pixeluv;
        vec2_mul(&scale, size);

        Vector2 relpos = pixeluv;
        vec2_mul(&relpos, &border);

#ifdef DEBUG_GLX
        printf_dbgf("SHADOW %f, %f, %f, %f\n", relpos.x, relpos.y, scale.x, scale.y);
#endif

        draw_rect(face, global_type->mvp, relpos, scale);
    }

    glDisable(GL_STENCIL_TEST);

    // @HACK This should not be done in the render function
    // Make texture for blur
    struct Texture swap;
    if(texture_init(&swap, GL_TEXTURE_2D, &texture.size) != 0) {
        printf("Failed allocating texture to blur shadow\n");
        texture_delete(&texture);
        renderbuffer_delete(&buffer);
        framebuffer_delete(&framebuffer);
        return;
    }

    // Do the blur
    struct TextureBlurData blurData = {
        .buffer = &framebuffer,
        .swap = &swap,
    };
    if(!texture_blur(&blurData, &texture, 4)) {
        printf_errf("Failed blurring the background texture");
        texture_delete(&texture);
        renderbuffer_delete(&buffer);
        framebuffer_delete(&framebuffer);
        if(hadScissor)
            glEnable(GL_SCISSOR_TEST);
        return;
    }

    texture_delete(&swap);

    struct Texture clipBuffer;
    if(texture_init(&clipBuffer, GL_TEXTURE_2D, &buffer.size) != 0) {
        printf("Failed creating clipping renderbuffer\n");
        texture_delete(&texture);
        renderbuffer_delete(&buffer);
        framebuffer_delete(&framebuffer);
        if(hadScissor)
            glEnable(GL_SCISSOR_TEST);
        return;
    }

    framebuffer_resetTarget(&framebuffer);
    framebuffer_targetTexture(&framebuffer, &clipBuffer);
    framebuffer_targetRenderBuffer_stencil(&framebuffer, &buffer);
    if(framebuffer_bind(&framebuffer) != 0) {
        printf("Failed binding framebuffer to clip shadow\n");

        texture_delete(&texture);
        renderbuffer_delete(&buffer);
        texture_delete(&clipBuffer);
        framebuffer_delete(&framebuffer);
        if(hadScissor)
            glEnable(GL_SCISSOR_TEST);
        return;
    }
    glViewport(0, 0, clipBuffer.size.x, clipBuffer.size.y);

    glClearColor(0.0, 0.0, 0.0, 0.0);
    glClear(GL_COLOR_BUFFER_BIT);

    glEnable(GL_STENCIL_TEST);

    glStencilMask(0xFF);
    glStencilFunc(GL_EQUAL, 0, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);

    draw_tex(ps, face, &texture, &VEC2_ZERO, &VEC2_UNIT);

    glDisable(GL_STENCIL_TEST);
    if(hadScissor)
        glEnable(GL_SCISSOR_TEST);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    static const GLenum DRAWBUFS[2] = { GL_BACK_LEFT };
    glDrawBuffers(1, DRAWBUFS);

    glViewport(0, 0, ps->root_width, ps->root_height);

    /* { */
    /*     Vector2 rpos = {{0, 0}}; */
    /*     Vector2 rsize = {{.4, .6}}; */
    /*     draw_tex(ps, face, &clipBuffer, &rpos, &rsize); */
    /* } */

    Vector2 root_size = {{ps->root_width, ps->root_height}};
    {
        Vector2 rpos = X11_rectpos_to_gl(ps, pos, size);
        vec2_sub(&rpos, &border);
        Vector2 rsize = overflowSize;

        Vector2 pixeluv = {{1.0f, 1.0f}};
        vec2_div(&pixeluv, &root_size);

        Vector2 scale = pixeluv;
        vec2_mul(&scale, &rsize);

        Vector2 relpos = pixeluv;
        vec2_mul(&relpos, &rpos);

        draw_tex(ps, face, &clipBuffer, &relpos, &scale);
    }

    glDisable(GL_STENCIL_TEST);
    glDisable(GL_BLEND);

    texture_delete(&texture);
    renderbuffer_delete(&buffer);
    texture_delete(&clipBuffer);
    framebuffer_delete(&framebuffer);
}
