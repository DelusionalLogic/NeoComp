#pragma once

#include "common.h"

#include "vmath.h"
#include "shaders/include.h"
#include "assets/face.h"

#define GL_GLEXT_PROTOTYPES
#include <GL/glx.h>

extern Matrix view;

void draw_rect(struct face* face, struct shader_value* mvp, Vector3 pos, Vector2 size);

void draw_colored_rect(struct face* face, Vector3* pos, Vector2* size, Vector4* color);

void draw_tex(struct face* face, const struct Texture* texture,
        const Vector3* pos, const Vector2* size);
