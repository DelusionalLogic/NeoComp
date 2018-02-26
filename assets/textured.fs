#version 130

in vec2 fragmentUV;
uniform sampler2D tex_scr;

uniform float opacity = 1.0;

void main() {
    vec2 uv = fragmentUV;
    gl_FragColor = texture2D(tex_scr, uv);
    gl_FragColor.a *= opacity;
    /* gl_FragColor = vec4(uv, 0, 1); */
}
