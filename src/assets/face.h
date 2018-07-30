#pragma once

#define GL_GLEXT_PROTOTYPES

#include "../vmath.h"

#include "../vector.h"

#include <GL/glx.h>
#include <Judy.h>

struct face {
    Vector vertex_buffer;
    Vector uv_buffer;

    GLuint vao;
    GLuint vertex;
    GLuint uv;
};

struct face* face_load_file(const char* path);

// @CLEANUP: This shouldn't be here
struct Rect {
    Vector2 pos;
    Vector2 size;
};

void face_init(struct face* asset, size_t vertex_count);
void face_init_rects(struct face* asset, Vector* rects);

void face_upload(struct face* asset);
void face_bind(struct face* face);

void face_unload_file(struct face* asset);
