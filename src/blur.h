#pragma once

#include "common.h"

#include "vmath.h"
#include "texture.h"
#include "assets/face.h"

#include <GL/glx.h>

/* typedef struct { */
/*   /// Framebuffer used for blurring. */
/*   GLuint fbo; */
/*   /// Textures used for blurring. */
/*   GLuint textures[2]; */
/*   struct Texture texture[2]; */
/*   Vector2 size; */
/*   /// Width of the textures. */
/*   int width; */
/*   /// Height of the textures. */
/*   int height; */
/* } glx_blur_cache_t; */

/* struct blur { */
/*     struct face* face; */
/*     GLuint array; */
/* }; */

void blur_init(struct blur* blur);
void blur_destroy(struct blur* blur);

bool blur_backbuffer(struct blur* blur, session_t* ps, const Vector2* pos,
        const Vector2* size, float z, GLfloat factor_center,
        XserverRegion reg_tgt, const reg_data_t *pcache_reg,
        glx_blur_cache_t* pbc);

int blur_cache_init(glx_blur_cache_t* cache, const Vector2* size);
