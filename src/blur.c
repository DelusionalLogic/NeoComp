#include "blur.h"

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

int blur_cache_init(glx_blur_cache_t* cache, const Vector2* size) {
    if(!vec2_eq(size, &cache->size)) {
        if(cache->textures[0] != 0) {
            glDeleteTextures(1, &cache->textures[0]);
            cache->textures[0] = 0;
        }
        if(cache->textures[1] != 0) {
            glDeleteTextures(1, &cache->textures[1]);
            cache->textures[1] = 0;
        }
        cache->width = 0;
        cache->height = 0;
    }

    // Generate textures if needed
    if(cache->textures[0] == 0)
        cache->textures[0] = generate_texture(GL_TEXTURE_2D, size);

    if(cache->textures[0] == 0) {
        printf("Failed allocating texture for cache\n");
        return 1;
    }

    if(cache->textures[1] == 0)
        cache->textures[1] = generate_texture(GL_TEXTURE_2D, size);

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
