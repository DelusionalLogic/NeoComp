#include "renderbuffer.h"

static GLuint generate_buffer(const Vector2* size, GLenum type) {
    GLuint b = 0;

    glGenRenderbuffers(1, &b);
    if(b == 0)
        return 0;

    glBindRenderbuffer(GL_RENDERBUFFER, b);

    glRenderbufferStorage(GL_RENDERBUFFER, type, size->x, size->y);

    glBindRenderbuffer(GL_RENDERBUFFER, 0);

    return b;
}

int renderbuffer_init(struct RenderBuffer* buffer, const Vector2* size) {
    buffer->gl_buffer = generate_buffer(size, GL_RGBA);
    if(buffer->gl_buffer == 0) {
        return 1;
    }

    buffer->type = BUFFERTYPE_COLOR;
    buffer->size = *size;

    return 0;
}

int renderbuffer_stencil_init(struct RenderBuffer* buffer, const Vector2* size) {
    buffer->gl_buffer = generate_buffer(size, GL_DEPTH24_STENCIL8);
    if(buffer->gl_buffer == 0) {
        return 1;
    }

    buffer->type = BUFFERTYPE_STENCIL;
    buffer->size = *size;

    return 0;
}

void renderbuffer_delete(struct RenderBuffer* buffer) {
    glDeleteRenderbuffers(1, &buffer->gl_buffer);
    buffer->gl_buffer = 0;
    buffer->size.x = 0;
    buffer->size.y = 0;
}

void renderbuffer_bind_to_framebuffer(struct RenderBuffer* buffer, GLenum attachment) {
    glFramebufferRenderbuffer(GL_DRAW_FRAMEBUFFER, attachment, GL_RENDERBUFFER,
            buffer->gl_buffer);
}
