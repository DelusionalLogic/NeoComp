#version 1

type downsample
vertex simple.vs
fragment downscale.fs
attrib 0 vertex
attrib 1 uv

uniform mvp ignored
uniform tex_scr sampler
uniform uvscale vec2 1,1
uniform pixeluv vec2 1,1
uniform flip bool
uniform extent vec2 1,1
