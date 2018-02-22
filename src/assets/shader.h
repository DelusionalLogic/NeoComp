#pragma once

#define GL_GLEXT_PROTOTYPES

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

