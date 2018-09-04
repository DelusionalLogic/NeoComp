#version 130

in vec2 fragmentUV;

uniform vec2 viewport;

uniform vec3 color;
uniform float opacity;

float rand(in vec2 co){
    return fract(sin(dot(co.xy, vec2(12.9898,78.233))) * 43758.5453);
}

void main(void){
    vec2 uv = fragmentUV;
    gl_FragColor = vec4(color, 1);
    gl_FragColor *= opacity;
    vec2 screen_uv = gl_FragCoord.xy / viewport;
    gl_FragColor.rgb *= vec3(1 - ((rand(screen_uv) - .5) * .2));
}
