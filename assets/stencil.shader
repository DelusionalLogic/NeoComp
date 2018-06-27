#version 1

type stencil
vertex simple.vs
fragment stencil.fs
attrib 0 vertex
attrib 1 uv

uniform mvp ignored
uniform tex_scr sampler
