#version 130
in vec3 vertex;
in vec2 uv;
out vec2 fragmentUV;

uniform mat4 MVP;
uniform vec2 uvscale;

void main() {
	fragmentUV = uv * uvscale;
	gl_Position = MVP * vec4(vertex, 1.0);
}
