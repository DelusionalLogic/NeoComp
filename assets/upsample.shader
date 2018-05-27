#version 1

type upsample
vertex simple.vs
fragment upsample.fs
attrib 0 vertex
attrib 1 uv

uniform mvp ignored
uniform flip bool
uniform tex_scr sampler
uniform uvscale vec2 1,1
uniform pixeluv vec2 1,1
uniform extent vec2 1,1
