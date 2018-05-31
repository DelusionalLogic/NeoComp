#define SHADER_NAME profiler
#define SHADER_INFO_NAME profiler_info
#define SHADER_STRUCT_NAME Profiler

#define UNIFORMS_FOREACH(M) \
    M(mvp)                  \
    M(opacity)              \
    M(color)
#define UNIFORMS_COUNT 3
