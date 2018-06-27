#define SHADER_NAME stencil
#define SHADER_INFO_NAME stencil_info
#define SHADER_STRUCT_NAME Stencil

#define UNIFORMS_FOREACH(M) \
    M(mvp)                  \
    M(tex_scr)
#define UNIFORMS_COUNT 2
