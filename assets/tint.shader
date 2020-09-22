#version 1

type colored
vertex simple.vs
fragment tint.fs
attrib 0 vertex
attrib 1 uv

uniform mvp ignored
uniform viewport vec2
uniform window vec2
uniform opacity float 1.0
uniform color vec3 1.0,1.0,1.0
