#pragma once

#include "vmath.h"
#include "texture.h"

#define GL_GLEXT_PROTOTYPES
#include <GL/glx.h>

typedef struct {
  /// Framebuffer used for blurring.
  GLuint fbo;
  /// Textures used for blurring.
  GLuint textures[2];
  struct Texture texture[2];
  Vector2 size;
  /// Width of the textures.
  int width;
  /// Height of the textures.
  int height;
} glx_blur_cache_t;

int blur_cache_init(glx_blur_cache_t* cache, const Vector2* size);
