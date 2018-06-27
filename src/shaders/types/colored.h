#define SHADER_NAME colored
#define SHADER_INFO_NAME colored_info
#define SHADER_STRUCT_NAME Colored

#define UNIFORMS_FOREACH(M) \
    M(mvp)                  \
    M(opacity)              \
    M(color)
#define UNIFORMS_COUNT 3
