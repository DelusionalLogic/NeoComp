#version 130

in vec2 fragmentUV;
uniform sampler2D tex_scr;

void main() {
    vec2 uv = fragmentUV;
    vec4 texcol = texture2D(tex_scr, uv);

    if(texcol.a == .0) {
        discard;
    }

    gl_FragColor = vec4(0, 0, 0, .4);
}
