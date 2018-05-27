#ifndef HEADER
#error "You need to define header before trying to create the shader"
#endif

#define TO_STR(A) #A

#define WRITE_MEMBER(name) \
    struct shader_value* name;

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

struct SHADER_STRUCT_NAME {
    UNIFORMS_FOREACH(WRITE_MEMBER)
};

#define MEMBER_COUNT (sizeof(struct SHADER_STRUCT_NAME) / sizeof(struct shader_value*))

#define EVAL2(name) TO_STR(name)
#define EVAL() EVAL2(SHADER_NAME)

extern struct shader_type_info SHADER_INFO_NAME;

#undef EVAL2
#undef EVAL

#undef SHADER_NAME
#undef UNIFORMS_FOREACH
#undef UNIFORMS_COUNT
#undef SHADER_INFO_NAME
#undef SHADER_STRUCT_NAME

#undef WRITE_MEMBER_INFO
#undef WRITE_MEMBER

#undef TO_STR
