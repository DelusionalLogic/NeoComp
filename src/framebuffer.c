#include "framebuffer.h"

#include <stdio.h>
#include <assert.h>

bool framebuffer_init(struct Framebuffer* framebuffer) {
    glGenFramebuffers(1, &framebuffer->gl_fbo);
    if(framebuffer->gl_fbo == 0) {
        return false;
    }
    framebuffer_resetTarget(framebuffer);
    return true;
}

bool framebuffer_initialized(struct Framebuffer* framebuffer) {
    return framebuffer->gl_fbo != 0;
}

void framebuffer_resetTarget(struct Framebuffer* framebuffer) {
    assert(framebuffer->gl_fbo != 0);
    framebuffer->target = 0;
}

void framebuffer_targetTexture(struct Framebuffer* framebuffer, struct Texture* texture) {
    assert(framebuffer->gl_fbo != 0);
    assert(texture_initialized(texture));
    if((framebuffer->target & FBT_TEXTURE) != 0) {
        printf("Framebuffer is already targeting a texture\n");
        return;
    }
    framebuffer->target |= FBT_TEXTURE;
    framebuffer->texture = texture;
}

void framebuffer_targetRenderBuffer(struct Framebuffer* framebuffer, struct RenderBuffer* buffer) {
    assert(framebuffer->gl_fbo != 0);
    assert(renderbuffer_initialized(buffer));
    if((framebuffer->target & FBT_RENDERBUFFER) != 0) {
        printf("Framebuffer is already targeting a renderbuffer\n");
        return;
    }
    framebuffer->target |= FBT_RENDERBUFFER;
    framebuffer->buffer = buffer;
}

void framebuffer_targetRenderBuffer_stencil(struct Framebuffer* framebuffer, struct RenderBuffer* buffer) {
    assert(framebuffer->gl_fbo != 0);
    assert(renderbuffer_initialized(buffer));
    if((framebuffer->target & FBT_RENDERBUFFER_STENCIL) != 0) {
        printf("Framebuffer is already targeting a renderbuffer stencil\n");
        return;
    }
    framebuffer->target |= FBT_RENDERBUFFER_STENCIL;
    framebuffer->buffer_stencil = buffer;
}

void framebuffer_targetBack(struct Framebuffer* framebuffer) {
    framebuffer->target |= FBT_BACKBUFFER;
}

int framebuffer_bind(struct Framebuffer* framebuffer) {
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer->gl_fbo);

    if(framebuffer->target == 0)
        return 0;

    return framebuffer_rebind(framebuffer);
}

int framebuffer_rebind(struct Framebuffer* framebuffer) {
    GLenum DRAWBUFS[4] = {0};
    size_t i = 0;

    if((framebuffer->target & FBT_BACKBUFFER) != 0) {
        DRAWBUFS[i++] = GL_BACK_LEFT;
    }
    if((framebuffer->target & FBT_TEXTURE)) {
        texture_bind_to_framebuffer_2(framebuffer->texture, GL_COLOR_ATTACHMENT1);
        DRAWBUFS[i++] = GL_COLOR_ATTACHMENT1;
    }
    if((framebuffer->target & FBT_RENDERBUFFER)) {
        renderbuffer_bind_to_framebuffer(framebuffer->buffer, GL_COLOR_ATTACHMENT0);
        DRAWBUFS[i++] = GL_COLOR_ATTACHMENT0;
    }

    if((framebuffer->target & FBT_RENDERBUFFER_STENCIL)) {
        renderbuffer_bind_to_framebuffer(framebuffer->buffer_stencil, GL_DEPTH_STENCIL_ATTACHMENT);
    }

    if (glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        printf("Framebuffer attachment failed\n");
        return 1;
    }

    glDrawBuffers(1, DRAWBUFS);
    return 0;
}

int framebuffer_bind_read(struct Framebuffer* framebuffer) {
    glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer->gl_fbo);
    return 0;
}

void framebuffer_delete(struct Framebuffer* framebuffer) {
    glDeleteFramebuffers(1, &framebuffer->gl_fbo);
    framebuffer_resetTarget(framebuffer);
    framebuffer->gl_fbo = 0;
}
