#version 130

in vec2 fragmentUV;
uniform sampler2D tex_scr;

uniform float opacity = 1.0;
uniform bool invert = false;

void main() {
    vec2 uv = fragmentUV;
    gl_FragColor = vec4(0, 0, 0, .2);
}
