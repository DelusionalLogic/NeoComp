#define SHADER_NAME shadow
#define SHADER_INFO_NAME shadow_info
#define SHADER_STRUCT_NAME Shadow

#define UNIFORMS_FOREACH(M) \
    M(mvp)                  \
    M(tex_scr)              \
    M(flip)
#define UNIFORMS_COUNT 3
