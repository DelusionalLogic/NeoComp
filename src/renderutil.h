#pragma once

#include "common.h"

#include "vmath.h"
#include "shaders/include.h"
#include "assets/face.h"

#define GL_GLEXT_PROTOTYPES
#include <GL/glx.h>

extern Matrix view;

void set_matrix(const struct shader_value* mvp, const Vector3 pos, const Vector2 size);
void draw_rect(const struct face* face, const struct shader_value* mvp, const Vector3 pos, const Vector2 size);
void draw_colored_rect(const struct face* face, const Vector3* pos, const Vector2* size, const Vector4* color);

void draw_tex(struct face* face, const struct Texture* texture,
        const Vector3* pos, const Vector2* size);
