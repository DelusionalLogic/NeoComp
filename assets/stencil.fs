#version 130

in vec2 fragmentUV;
uniform sampler2D tex_scr;

void main() {
    vec2 uv = fragmentUV;
    gl_FragColor = texture2D(tex_scr, uv);
    if(gl_FragColor.a <= .1) {
        discard;
    }
}
