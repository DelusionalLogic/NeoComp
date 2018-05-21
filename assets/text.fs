#version 130

in vec2 fragmentUV;
uniform sampler2D tex_scr;

uniform float opacity = 1.0;

void main() {
    vec2 uv = fragmentUV;
    vec4 sampled = texture2D(tex_scr, uv);
    gl_FragColor = vec4(1.0, 1.0, 1.0, sampled.r);
}
