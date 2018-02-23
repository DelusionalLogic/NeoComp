#pragma once

#define GL_GLEXT_PROTOTYPES
#include <GL/glx.h>
#include "vmath.h"

typedef struct {
  /// Framebuffer used for blurring.
  GLuint fbo;
  /// Textures used for blurring.
  GLuint textures[2];
  Vector2 size;
  /// Width of the textures.
  int width;
  /// Height of the textures.
  int height;
} glx_blur_cache_t;

void blur_cache_init(glx_blur_cache_t* cache, const Vector2* size);
