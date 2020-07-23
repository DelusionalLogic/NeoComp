#version 1

type bgblit
vertex double.vs
fragment bgblit.fs
attrib 0 vertex
attrib 1 uv

uniform mvp ignored
uniform win_tran mat4 identity
uniform invert bool false
uniform flip bool false
uniform opacity float 1.0
uniform tex_scr sampler
uniform win_tex sampler
