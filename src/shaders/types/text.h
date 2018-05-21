#define SHADER_NAME text
#define SHADER_INFO_NAME text_info
#define SHADER_STRUCT_NAME Text

#define UNIFORMS_FOREACH(M) \
    M(mvp)                  \
    M(tex_scr)              \
    M(flip)                 \
    M(opacity)
#define UNIFORMS_COUNT 4
