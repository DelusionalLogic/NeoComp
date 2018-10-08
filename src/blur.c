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

void blur_destroy(struct blur* blur) {
    glDeleteVertexArrays(1, &blur->array);
}

bool blur_cache_resize(glx_blur_cache_t* cache, const Vector2* size) {
    assert(renderbuffer_initialized(&cache->stencil));
    assert(texture_initialized(&cache->texture[0]));
    assert(texture_initialized(&cache->texture[1]));

    cache->size = *size;

    renderbuffer_resize(&cache->stencil, size);
    texture_resize(&cache->texture[0], size);
    texture_resize(&cache->texture[1], size);
    return true;
}

int blur_cache_init(glx_blur_cache_t* cache) {
    assert(!renderbuffer_initialized(&cache->stencil));
    assert(!texture_initialized(&cache->texture[0]));
    assert(!texture_initialized(&cache->texture[1]));

    if(renderbuffer_stencil_init(&cache->stencil, NULL) != 0) {
        printf("Failed allocating stencil for cache\n");
        return 1;
    }

    if(texture_init(&cache->texture[0], GL_TEXTURE_2D, NULL) != 0) {
        printf("Failed allocating texture for cache\n");
        renderbuffer_delete(&cache->stencil);
        return 1;
    }

    if(texture_init(&cache->texture[1], GL_TEXTURE_2D, NULL) != 0) {
        printf("Failed allocating texture for cache\n");
        renderbuffer_delete(&cache->stencil);
        texture_delete(&cache->texture[0]);
        return 1;
    }

    return 0;
}

void blur_cache_delete(glx_blur_cache_t* cache) {
    assert(renderbuffer_initialized(&cache->stencil));
    assert(texture_initialized(&cache->texture[0]));
    assert(texture_initialized(&cache->texture[1]));

    renderbuffer_delete(&cache->stencil);
    texture_delete(&cache->texture[0]);
    texture_delete(&cache->texture[1]);
}
