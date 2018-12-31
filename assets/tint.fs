#version 130

in vec2 fragmentUV;

uniform vec2 viewport;

uniform vec3 color;
uniform float opacity;

float rand(in vec2 co) {
    return fract(sin(dot(co.xy, vec2(12.9898,78.233))) * 43758.5453);
}

void main(void) {
    vec2 screen_uv = gl_FragCoord.xy / viewport;
    /* if(rand(screen_uv) < .85) */
        /* discard; */

    gl_FragColor = vec4(color, 1.0);
    gl_FragColor *= rand(screen_uv) * opacity;
}
