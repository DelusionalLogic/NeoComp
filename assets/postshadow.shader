#version 1

type postshadow
vertex simple.vs
fragment dithered.fs
attrib 0 vertex
attrib 1 uv

uniform mvp ignored
uniform flip bool false
uniform tex_scr sampler
uniform noise_scr sampler
