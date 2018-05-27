#version 1

type shadow
vertex simple.vs
fragment shadow.fs
attrib 0 vertex
attrib 1 uv

uniform mvp ignored
uniform tex_scr sampler

uniform flip bool false
uniform uvscale vec2 1.0,1.0
