#define SHADER_NAME upsample
#define SHADER_INFO_NAME upsample_info
#define SHADER_STRUCT_NAME Upsample

#define UNIFORMS_FOREACH(M) \
    M(mvp)                  \
    M(uvscale)              \
    M(flip)                 \
    M(pixeluv)              \
    M(tex_scr)              \
    M(extent)
#define UNIFORMS_COUNT 6
