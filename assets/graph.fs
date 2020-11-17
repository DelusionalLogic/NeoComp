
#version 140

in vec2 fragmentUV;

uniform samplerBuffer sampler;

uniform vec3 color;
uniform float opacity;
// @HACK: These should really be ints, but we don't have support for that yet
// in the shader system
uniform float width;
uniform float cursor;

void main() {
    vec2 uv = fragmentUV;

    float x = uv.x * width;
    int below = int(floor(x));
    below = (below + int(cursor)) % int(width);
    vec4 texel = texelFetch(sampler, below);

    float distance = abs(uv.y - texel.r);

    // Thanks to graphy for the style for this cool graph shader.
    vec4 pcolor = vec4(0.0);
    if (uv.y > texel.r) {
        gl_FragColor = vec4(0, 0, 0, 0);
        return;
    }

    gl_FragColor = vec4(color, 1.0);

    if(texel.r - uv.y > 0.02) {
        gl_FragColor *= uv.y * 0.3 / texel.r;
    }

    if (uv.x < 0.03) {
        gl_FragColor *= 1 - (0.03 - uv.x) / 0.03;
    }
    else if (uv.x > 0.97) {
        gl_FragColor *= (1 - uv.x) / 0.03;
    }
}
