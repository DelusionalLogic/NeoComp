#pragma once

#define GL_GLEXT_PROTOTYPES

#include "../vmath.h"

#include <GL/glx.h>
#include <Judy.h>

struct shader {
    GLuint gl_shader;
};

struct shader* vert_shader_load_file(const char* path);
struct shader* frag_shader_load_file(const char* path);

void shader_unload_file(struct shader* asset);

struct shader_program {
    struct shader_type_info* shader_type_info;
    void* shader_type;
    struct shader* fragment;
    struct shader* vertex;
    Pvoid_t attributes;
    GLuint gl_program;
};
struct shader_program* shader_program_load_file(const char* path);
void shader_program_unload_file(struct shader_program* asset);

void shader_use(const struct shader_program* shader);

void shader_set_uniform_vec2(GLint location, const Vector2* value);
void shader_set_uniform_sampler(GLint location, int value);
