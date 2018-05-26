#define SHADER_NAME passthough
#define SHADER_INFO_NAME passthough_info
#define SHADER_STRUCT_NAME Passthough

#define UNIFORMS_FOREACH(M) \
    M(mvp)                  \
    M(flip)                 \
    M(opacity)              \
    M(tex_scr)
#define UNIFORMS_COUNT 4
