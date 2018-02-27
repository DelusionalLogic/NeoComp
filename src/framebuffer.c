#include "framebuffer.h"

#include <stdio.h>

bool framebuffer_init(struct Framebuffer* framebuffer) {
    glGenFramebuffers(1, &framebuffer->gl_fbo);
    if(framebuffer->gl_fbo == 0) {
        return false;
    }
    framebuffer_resetTarget(framebuffer);
    return true;
}

void framebuffer_resetTarget(struct Framebuffer* framebuffer) {
    framebuffer->target = 0;
    framebuffer->texture = NULL;
}

void framebuffer_targetTexture(struct Framebuffer* framebuffer, struct Texture* texture) {
    if((framebuffer->target & FBT_TEXTURE) != 0) {
        printf("Framebuffer is already targeting a texture");
        return;
    }
    framebuffer->target |= FBT_TEXTURE;
    framebuffer->texture = texture;
}

void framebuffer_targetBack(struct Framebuffer* framebuffer) {
    framebuffer->target |= FBT_BACKBUFFER;
}

bool framebuffer_bind(struct Framebuffer* framebuffer) {
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer->gl_fbo);

    if((framebuffer->target & FBT_TEXTURE)) {
        texture_bind_to_framebuffer_2(framebuffer->texture, GL_COLOR_ATTACHMENT0);
    }

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) !=
            GL_FRAMEBUFFER_COMPLETE) {
        printf("Framebuffer attachment failed\n");
        return 1;
    }

    GLenum DRAWBUFS[3] = {0};
    size_t i = 0;
    if((framebuffer->target & FBT_BACKBUFFER) != 0) {
        DRAWBUFS[i++] = GL_BACK_LEFT;
    }
    if((framebuffer->target & FBT_TEXTURE) != 0) {
        DRAWBUFS[i++] = GL_COLOR_ATTACHMENT0;
    }
    glDrawBuffers(1, DRAWBUFS);

    return 0;
}

void framebuffer_delete(struct Framebuffer* framebuffer) {
    glDeleteFramebuffers(1, &framebuffer->gl_fbo);
    framebuffer->gl_fbo = 0;
    framebuffer_resetTarget(framebuffer);
}
