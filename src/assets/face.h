#pragma once

#define GL_GLEXT_PROTOTYPES

#include "../vmath.h"

#include <GL/glx.h>
#include <Judy.h>

struct face {
    float* vertex_buffer_data;
    float* uv_buffer_data;

    GLuint vertex;
    GLuint uv;
};

struct face* face_load_file(const char* path);

void face_unload_file(struct face* asset);
