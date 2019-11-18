#version 1

type bgblit
vertex simple.vs
fragment bgblit.fs
attrib 0 vertex
attrib 1 uv

uniform mvp ignored
uniform flip bool false
uniform opacity float 1.0
uniform tex_scr sampler
uniform win_tex sampler
