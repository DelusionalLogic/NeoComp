#version 140

in vec2 fragmentUV;

uniform samplerBuffer sampler;
uniform vec3 color;
uniform float opacity;
uniform float width;

void main() {
    vec2 uv = fragmentUV;

    float x = uv.x * width;
    int below = int(floor(x));
    vec4 texel = texelFetch(sampler, below);

    float distance = abs(uv.y - texel.r);
    float reldist = distance / texel.r;

    vec4 pcolor = vec4(0.0);

    if(distance < .004) {
        pcolor = vec4(color, 1.0);
    }

    if(uv.y < texel.r) {
        pcolor = mix(vec4(0.0), vec4(color, 1), clamp(pow(1.0-reldist, 3), 0, 1));
    }

    gl_FragColor = pcolor;
}
