#define SHADER_NAME global
#define SHADER_INFO_NAME global_info
#define SHADER_STRUCT_NAME Global

#define UNIFORMS_FOREACH(M) \
    M(mvp)                  \
    M(tex_scr)              \
    M(flip)                 \
    M(invert)               \
    M(dim)                  \
    M(uvscale)              \
    M(opacity)
#define UNIFORMS_COUNT 7
