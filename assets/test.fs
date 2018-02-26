#version 130

in vec2 fragmentUV;
uniform sampler2D tex_scr;

uniform float opacity = 1.0;
uniform bool invert = false;

void main() {
    vec2 uv = fragmentUV;
    gl_FragColor = texture2D(tex_scr, uv);
    if(invert)
        gl_FragColor.rgb = vec3(gl_FragColor.a) - gl_FragColor.rgb;
    gl_FragColor *= opacity;
}
