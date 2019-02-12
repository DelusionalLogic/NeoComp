#define SHADER_NAME graph
#define SHADER_INFO_NAME graph_info
#define SHADER_STRUCT_NAME Graph

#define UNIFORMS_FOREACH(M) \
    M(mvp)                  \
    M(color)                \
    M(width)                \
    M(sampler)
#define UNIFORMS_COUNT 4
