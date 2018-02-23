#version 130
uniform vec2 pixeluv;
in vec2 fragmentUV;
uniform vec2 extent;
uniform sampler2D tex_scr;

vec4 sample(vec2 uv) {
    return texture2D(tex_scr, clamp(uv, vec2(0.0), extent));
}

void main() {
    vec2 uv = fragmentUV;
    vec4 sum = sample(uv + vec2(-pixeluv.x * 2.0, 0.0));
    sum += sample(uv + vec2(-pixeluv.x, pixeluv.y)) * 2.0;
    sum += sample(uv + vec2(0.0, pixeluv.y * 2.0));
    sum += sample(uv + vec2(pixeluv.x, pixeluv.y)) * 2.0;
    sum += sample(uv + vec2(pixeluv.x * 2.0, 0.0));
    sum += sample(uv + vec2(pixeluv.x, -pixeluv.y)) * 2.0;
    sum += sample(uv + vec2(0.0, -pixeluv.y * 2.0));
    sum += sample(uv + vec2(-pixeluv.x, -pixeluv.y)) * 2.0;
    gl_FragColor = sum / 12.0;
}
