#version 1

type text
vertex simple.vs
fragment text.fs
attrib 0 vertex
attrib 1 uv

uniform mvp ignored
uniform tex_scr sampler
uniform flip bool false
uniform opacity float 1.0
