#pragma once

#include "vmath.h"
#include "assets/face.h"

#define GL_GLEXT_PROTOTYPES
#include <GL/glx.h>

void draw_rect(struct face* face, GLuint mvp, Vector2 pos, Vector2 size);
