#define GL_GLEXT_PROTOTYPES
#include "blur.h"

#include "assets/assets.h"
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

void blur_destroy(struct blur* blur) {
    glDeleteVertexArrays(1, blur->array);
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
    if(cache->fbo == 0)
        glGenFramebuffers(1, &cache->fbo);

    if(cache->fbo == 0) {
        printf("Failed allocating framebuffer for cache\n");
        return 1;
    }

    // Set the size
    cache->size = *size;

    return 0;
}
