#define SHADER_NAME bgblit
#define SHADER_INFO_NAME bgblit_info
#define SHADER_STRUCT_NAME BgBlit

#define UNIFORMS_FOREACH(M) \
    M(mvp)                  \
    M(flip)                 \
    M(opacity)              \
    M(tex_scr)              \
    M(win_tex)
#define UNIFORMS_COUNT 5
