#version 130

in vec2 fragmentUV;
uniform sampler2D tex_scr;
uniform sampler2D win_tex;

uniform float opacity = 1.0;

void main() {
    vec2 uv = fragmentUV;
    if(texture2D(win_tex, uv).a == 1) {
        discard;
    }
    gl_FragColor = texture2D(tex_scr, uv);
    gl_FragColor *= opacity;
}
