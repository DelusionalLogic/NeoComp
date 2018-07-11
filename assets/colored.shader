#version 1

type colored
vertex simple.vs
fragment colored.fs
attrib 0 vertex
attrib 1 uv

uniform mvp ignored
uniform opacity float 1.0
uniform color vec3
