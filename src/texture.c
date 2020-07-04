#include "texture.h"

#include <stdio.h>
#include <assert.h>

#include "profiler/zone.h"

DECLARE_ZONE(texture_resize);

static GLuint generate_texture(GLenum tex_tgt, GLint format, const Vector2* size) {
    GLuint tex = 0;

    glGenTextures(1, &tex);
    if (!tex)
        return 0;

    glBindTexture(tex_tgt, tex);
    glTexParameteri(tex_tgt, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(tex_tgt, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(tex_tgt, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(tex_tgt, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    if(size != NULL)
        glTexImage2D(tex_tgt, 0, format, size->x, size->y, 0, GL_RGBA,
                GL_UNSIGNED_BYTE, NULL);

    return tex;
}

int texture_init_noise(struct Texture* texture, GLenum target) {
    static const char pattern[] = {
         0, 32,  8, 40,  2, 34, 10, 42,   /* 8x8 Bayer ordered dithering  */
        48, 16, 56, 24, 50, 18, 58, 26,  /* pattern.  Each input pixel   */
        12, 44,  4, 36, 14, 46,  6, 38,  /* is scaled to the 0..63 range */
        60, 28, 52, 20, 62, 30, 54, 22,  /* before looking in this table */
         3, 35, 11, 43,  1, 33,  9, 41,   /* to determine the action.     */
        51, 19, 59, 27, 49, 17, 57, 25,
        15, 47,  7, 39, 13, 45,  5, 37,
        63, 31, 55, 23, 61, 29, 53, 21
    };

    glGenTextures(1, &texture->gl_texture);
    if(!texture->gl_texture)
        return 1;
    glBindTexture(target, texture->gl_texture);
    glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexImage2D(target, 0, GL_RED, 8, 8, 0, GL_RED, GL_UNSIGNED_BYTE,
            pattern);

    texture->target = target;
    texture->size = (Vector2){{8, 8}};
    texture->hasSpace = true;

    return 0;
}

int texture_init(struct Texture* texture, GLenum target, const Vector2* size) {
    texture->gl_texture = generate_texture(target, GL_RGBA8, size);
    if(texture->gl_texture == 0) {
        return 1;
    }

    texture->target = target;

    // If the size was NULL, then we didn't allocate any space in the
    // generate_texture call
    if(size != NULL) {
        texture->size = *size;
        texture->hasSpace = true;
    } else {
        texture->hasSpace = false;
    }

    return 0;
}

int texture_init_hp(struct Texture* texture, GLenum target, const Vector2* size) {
    texture->gl_texture = generate_texture(target, GL_RGBA32F, size);
    if(texture->gl_texture == 0) {
        return 1;
    }

    texture->target = target;

    // If the size was NULL, then we didn't allocate any space in the
    // generate_texture call
    if(size != NULL) {
        texture->size = *size;
        texture->hasSpace = true;
    } else {
        texture->hasSpace = false;
    }

    return 0;
}

int texture_init_buffer(struct Texture* texture, const size_t size, struct BufferObject* bo, GLenum format) {
    GLenum target = GL_TEXTURE_BUFFER;
    texture->gl_texture = generate_texture(target, GL_RGBA8, NULL);
    if(texture->gl_texture == 0) {
        return 1;
    }

    glTexBuffer(target, format, bo->gl);

    texture->target = target;
    texture->size = (Vector2){{size, 1}};
    texture->hasSpace = true;

    return 0;
}

int texture_init_nospace(struct Texture* texture, GLenum target, const Vector2* size) {
    texture->gl_texture = generate_texture(target, GL_RGBA8, size);
    if(texture->gl_texture == 0) {
        return 1;
    }

    texture->target = target;
    if(size != NULL)
        texture->size = *size;

    return 0;
}

void texture_resize(struct Texture* texture, const Vector2* size) {
    zone_scope(&ZONE_texture_resize);
    assert(texture_initialized(texture));

    glBindTexture(texture->target, texture->gl_texture);
    glTexImage2D(texture->target, 0, GL_RGBA, size->x, size->y, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glBindTexture(texture->target, 0);

    texture->hasSpace = true;
    texture->size = *size;
}

void texture_delete(struct Texture* texture) {
    glDeleteTextures(1, &texture->gl_texture);
    texture->gl_texture = 0;
    texture->target = 0;
    texture->size.x = 0;
    texture->size.y = 0;
}

bool texture_initialized(const struct Texture* texture) {
    return texture->gl_texture != 0;
}

int texture_read_from(struct Texture* texture, GLuint framebuffer,
        GLenum buffer, const Vector2* pos, const Vector2* size) {
    if(texture->size.x < size->x || texture->size.y < size->y) {
        printf("Tried to copy an area too large into a texture\n");
        return 1;
    }

    glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer);
    glReadBuffer(buffer);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(texture->target, texture->gl_texture);

    if (size->x <= 0 && size->y <= 0) {
        return 1;
    }

    Vector2 offset = {{0, 0}};
    glCopyTexSubImage2D(texture->target, 0, offset.x, offset.y,
            pos->x, pos->y, size->x, size->y);

    return 0;
}

int texture_bind_to_framebuffer(struct Texture* texture, GLuint framebuffer,
        GLenum buffer) {
    assert(texture->hasSpace);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
            texture->target, texture->gl_texture, 0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) !=
            GL_FRAMEBUFFER_COMPLETE) {
        printf("Framebuffer attachment failed\n");
        return 1;
    }

    return 0;
}

void texture_bind_to_framebuffer_2(struct Texture* texture, GLenum target) {
    assert(texture != NULL);
    assert(texture_initialized(texture));
    assert(texture->hasSpace);

    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, target, texture->target,
            texture->gl_texture, 0);
}

void texture_bind(const struct Texture* texture, GLenum unit) {
    assert(texture != NULL);
    assert(texture_initialized(texture));

    glActiveTexture(unit);
    glBindTexture(texture->target, texture->gl_texture);
}
