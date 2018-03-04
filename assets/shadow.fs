#version 130

in vec2 fragmentUV;
uniform sampler2D tex_scr;

uniform float opacity = 1.0;
uniform bool invert = false;

void main() {
    vec2 uv = fragmentUV;
    vec4 texcol = texture2D(tex_scr, uv);

    if(texcol.a <= .00001) {
        discard;
    }

    gl_FragColor = texcol;
    gl_FragColor.rgb *= 1;
    gl_FragColor.a = 1;

}
