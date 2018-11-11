#version 130

in vec2 fragmentUV;
uniform sampler2D tex_scr;

uniform float opacity = 1.0;
uniform vec3 color = vec3(1);

void main() {
    vec2 uv = fragmentUV;
    vec4 sampled = texture2D(tex_scr, uv);
    gl_FragColor = vec4(color, 1) * sampled.r;
}
