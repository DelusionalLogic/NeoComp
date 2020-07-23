#version 130

in vec2 tex_uv;
uniform sampler2D tex_scr;

in vec2 win_uv;
uniform sampler2D win_tex;

uniform float opacity = 1.0;

/* If this is false we do an intersection, otherwise it's a cut */
uniform bool invert = false;

void main() {
    if((win_uv.x > 0 && win_uv.x < 1.0) &&
            win_uv.y > 0 && win_uv.y < 1.0) {
        if(!invert && texture2D(win_tex, win_uv).a <= .0) {
            discard;
        }

        if(invert && texture2D(win_tex, win_uv).a > .0) {
            discard;
        }
    }

    /* if((win_uv.x > 0 && win_uv.x < 1.0) && */
    /*     win_uv.y > 0 && win_uv.y < 1.0) { */
    /*     gl_FragColor = vec4(win_uv, 0.0, 1.0); */
    /*     gl_FragColor = texture2D(win_tex, win_uv); */
    /*     return; */
    /* } */

    gl_FragColor = texture2D(tex_scr, tex_uv);
    gl_FragColor *= opacity;
}
