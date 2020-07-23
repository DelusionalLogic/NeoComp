#version 130
in vec3 vertex;
in vec2 uv;
out vec2 tex_uv;
out vec2 win_uv;

uniform mat4 mvp;
uniform mat4 win_tran;

uniform bool flip = false;

void main() {
    tex_uv = flip ? vec2(uv.x, 1 - uv.y) : uv;
    win_uv = (win_tran * vec4(tex_uv, 1.0, 1.0)).xy;

    gl_Position = mvp * vec4(vertex, 1.0);
}
