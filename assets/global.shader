#version 1

type global
vertex simple.vs
fragment test.fs
attrib 0 vertex
attrib 1 uv

uniform mvp ignored
uniform flip bool
uniform tex_scr sampler

uniform invert bool false
uniform dim float 0.0
uniform opacity float 1.0
