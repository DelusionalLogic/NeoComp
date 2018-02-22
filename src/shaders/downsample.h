#define SHADER_NAME downsample
#define SHADER_INFO_NAME downsample_info
#define SHADER_STRUCT_NAME Downsample

#define UNIFORMS_FOREACH(M) \
    M(mvp)                  \
    M(uvscale)              \
    M(pixeluv)              \
    M(tex_scr)              \
    M(extent)
#define UNIFORMS_COUNT 5
