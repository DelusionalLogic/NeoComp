#pragma once

#include "common.h"

#include "vmath.h"
#include "assets/face.h"

#define GL_GLEXT_PROTOTYPES
#include <GL/glx.h>

void draw_rect(struct face* face, GLuint mvp, Vector2 pos, Vector2 size);

void draw_tex(session_t* ps, struct face* face, const struct Texture* texture,
        const Vector2* pos, const Vector2* size);
