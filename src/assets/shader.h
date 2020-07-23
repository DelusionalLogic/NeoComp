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

#define SHADER_UNIFORMS_MAX 8

enum shader_value_type {
    SHADER_VALUE_BOOL,
    SHADER_VALUE_FLOAT,
    SHADER_VALUE_VEC2,
    SHADER_VALUE_VEC3,
    SHADER_VALUE_MAT4,
    SHADER_VALUE_SAMPLER,
    SHADER_VALUE_IGNORED,
};

union shader_uniform_value {
    bool boolean;
    double flt;
    Vector2 vector;
    Vector3 vec3;
    Matrix mat4;
    GLint sampler;
};

struct shader_value {
    GLuint gl_uniform;
    enum shader_value_type type;

    bool required;
    union shader_uniform_value stock;

    bool set;
    union shader_uniform_value value;
};

struct shader_program {
    struct shader_type_info* shader_type_info;
    void* shader_type;
    struct shader* fragment;
    struct shader* vertex;
    Pvoid_t attributes;
    GLuint gl_program;

    size_t uniforms_num;
    struct shader_value uniforms[SHADER_UNIFORMS_MAX];
};
struct shader_program* shader_program_load_file(const char* path);
void shader_program_unload_file(struct shader_program* asset);

void shader_use(struct shader_program* shader);

void shader_set_uniform_bool(const struct shader_value* location, bool value);
void shader_set_uniform_float(const struct shader_value* location, float value);
void shader_set_uniform_vec2(const struct shader_value* location, const Vector2* value);
void shader_set_uniform_vec3(const struct shader_value* uniform, const Vector3* value);
void shader_set_uniform_mat4(const struct shader_value* uniform, const Matrix* value);
void shader_set_uniform_sampler(const struct shader_value* location, int value);

void shader_set_future_uniform_bool(struct shader_value* location, bool value);
void shader_set_future_uniform_float(struct shader_value* location, float value);
void shader_set_future_uniform_vec2(struct shader_value* location, const Vector2* value);
void shader_set_future_uniform_vec3(struct shader_value* location, const Vector3* value);
void shader_set_future_uniform_sampler(struct shader_value* location, int value);

void shader_clear_future_uniform(struct shader_value* uniform);
