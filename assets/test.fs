#version 130

in vec2 fragmentUV;
uniform sampler2D tex_scr;

uniform float opacity = 1.0;
uniform float dim;
uniform bool invert = false;

void main() {
    vec2 uv = fragmentUV;
    gl_FragColor = texture2D(tex_scr, uv);

    vec3 contrib = gl_FragColor.rgb * vec3(0.2627, 0.6780, 0.0593);
    float luma = contrib.r + contrib.g + contrib.b;
    gl_FragColor.rgb += (1.0 - dim) * (vec3(luma) - gl_FragColor.rgb);

    gl_FragColor.rgb *= .2 * dim + .8;

    if(invert) {
        gl_FragColor.rgb = vec3(gl_FragColor.a) - gl_FragColor.rgb;
    }

    gl_FragColor *= opacity;

    if(gl_FragColor.a == 0)
        discard;
}
