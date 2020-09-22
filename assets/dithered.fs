#version 130

in vec2 fragmentUV;
uniform sampler2D tex_scr;
uniform sampler2D noise_scr;

float sampleNoise(vec2 uv) {
    vec4 noise = texture2D(noise_scr, uv / 8.0);
    return noise.r / 64.0 - (1.0 / 128.0);
}

void main() {
    vec2 uv = fragmentUV;
    gl_FragColor = texture2D(tex_scr, uv);
    gl_FragColor += vec4(sampleNoise(gl_FragCoord.xy));
}
