#version 130

in vec2 fragmentUV;
uniform sampler2D tex_scr;

void main() {
    vec2 uv = fragmentUV;
    gl_FragColor = vec4(.1, .1, .2, 1);
}
