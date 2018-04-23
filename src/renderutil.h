#pragma once

#include "common.h"

#include "vmath.h"
#include "assets/face.h"

#define GL_GLEXT_PROTOTYPES
#include <GL/glx.h>

extern Matrix view;

void draw_rect(struct face* face, GLuint mvp, Vector3 pos, Vector2 size);

void draw_tex(session_t* ps, struct face* face, const struct Texture* texture,
        const Vector3* pos, const Vector2* size);
