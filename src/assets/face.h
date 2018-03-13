#pragma once

#define GL_GLEXT_PROTOTYPES

#include "../vmath.h"

#include <GL/glx.h>
#include <Judy.h>

struct face {
    float* vertex_buffer_data;
    size_t vertex_buffer_size;
    float* uv_buffer_data;
    size_t uv_buffer_size;

    GLuint vertex;
    GLuint uv;
};

struct face* face_load_file(const char* path);

void face_unload_file(struct face* asset);
