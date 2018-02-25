#ifndef HEADER
#error "You need to define header before trying to create the shader"
#endif

#define WRITE_MEMBER_INFO(name)    \
    {TO_STR(name), offsetof(struct SHADER_STRUCT_NAME, name)},

#include THIS

#ifndef SHADER_NAME
#error "Shader must have a name"
#endif

#ifndef UNIFORMS_FOREACH
#error "UNIFORMS_FOREACH most be defined by the shader"
#endif

#ifndef UNIFORMS_COUNT
#error "UNIFORMS_COUNT most be defined by the shader"
#endif

#ifndef SHADER_INFO_NAME
#error "SHADER_INFO_NAME most be defined by the shader"
#endif

#ifndef SHADER_STRUCT_NAME
#error "SHADER_STRUCT_NAME most be defined by the shader"
#endif

#define TO_STR(A) #A
#define EVAL2(name) TO_STR(name)
#define EVAL() EVAL2(SHADER_NAME)

extern struct shader_type_info SHADER_INFO_NAME = {
    .name = EVAL(),
    .size = sizeof(struct SHADER_STRUCT_NAME),
    .member_count = UNIFORMS_COUNT,
    .members = {
        UNIFORMS_FOREACH(WRITE_MEMBER_INFO)
    }
};

#undef EVAL
#undef EVAL2
#undef TO_STR

#undef SHADER_NAME
#undef UNIFORMS_FOREACH
#undef UNIFORMS_COUNT
#undef SHADER_INFO_NAME
#undef SHADER_STRUCT_NAME

#undef WRITE_MEMBER_INFO
