#version 1

type passthough
vertex simple.vs
fragment textured.fs
attrib 0 vertex
attrib 1 uv

uniform mvp ignored
uniform flip bool false
uniform opacity float 1.0
uniform tex_scr sampler
