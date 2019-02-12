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

    if(distance < .004) {
        gl_FragColor = vec4(color, 1.0);
        return;
    }

    if(uv.y < texel.r) {
        vec4 pcolor = vec4(color, 1);
        pcolor *= .6;
        gl_FragColor = pcolor * (uv.y/texel.r);
        return;
    }

    gl_FragColor = vec4(0);
}
