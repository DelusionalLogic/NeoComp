#include "renderbuffer.h"

#include <assert.h>

static GLuint generate_buffer(const Vector2* size, GLenum type) {
    GLuint b = 0;

    glGenRenderbuffers(1, &b);
    if(b == 0)
        return 0;

    glBindRenderbuffer(GL_RENDERBUFFER, b);

    // If we don't get a size, just don't allocate any storage for now
    if(size != NULL)
        glRenderbufferStorage(GL_RENDERBUFFER, type, size->x, size->y);


    return b;
}

// FOR ALL INIT FUNCTIONS: We can give them NULL for size to specify that we
// don't want any storage
int renderbuffer_init(struct RenderBuffer* buffer, const Vector2* size) {
    buffer->gl_type = GL_RGBA;
    buffer->gl_buffer = generate_buffer(size, buffer->gl_type);
    if(buffer->gl_buffer == 0) {
        return 1;
    }

    buffer->type = BUFFERTYPE_COLOR;
    if(size != NULL)
        buffer->size = *size;

    return 0;
}

int renderbuffer_stencil_init(struct RenderBuffer* buffer, const Vector2* size) {
    buffer->gl_type = GL_DEPTH24_STENCIL8;
    buffer->gl_buffer = generate_buffer(size, buffer->gl_type);
    if(buffer->gl_buffer == 0) {
        return 1;
    }

    buffer->type = BUFFERTYPE_STENCIL;

    // If the size was NULL, then we didn't allocate any space in the
    // generate_buffer call
    if(size != NULL) {
        buffer->size = *size;
        buffer->hasSpace = true;
    } else {
        buffer->hasSpace = false;
    }

    return 0;
}

bool renderbuffer_initialized(struct RenderBuffer* buffer) {
    return buffer->gl_buffer != 0;
}

void renderbuffer_resize(struct RenderBuffer* buffer, const Vector2* size) {
    assert(buffer->gl_buffer != 0);

    glBindRenderbuffer(GL_RENDERBUFFER, buffer->gl_buffer);
    glRenderbufferStorage(GL_RENDERBUFFER, buffer->gl_type, size->x, size->y);

    buffer->hasSpace = true;
    buffer->size = *size;
}


void renderbuffer_delete(struct RenderBuffer* buffer) {
    glDeleteRenderbuffers(1, &buffer->gl_buffer);
    buffer->gl_buffer = 0;
    buffer->size.x = 0;
    buffer->size.y = 0;
}

void renderbuffer_bind_to_framebuffer(struct RenderBuffer* buffer, GLenum attachment) {
    assert(buffer != NULL);
    assert(renderbuffer_initialized(buffer));
    assert(buffer->hasSpace);

    glFramebufferRenderbuffer(GL_DRAW_FRAMEBUFFER, attachment, GL_RENDERBUFFER,
            buffer->gl_buffer);
}
