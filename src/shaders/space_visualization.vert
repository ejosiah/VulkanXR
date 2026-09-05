#version 450

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;

layout(location = 0) out vec4 vertex_color;

layout(push_constant) uniform constants {
    mat4 model;
    mat4 view_projection;
} pc;

void main() {
    vertex_color = vec4(abs(normal), 1);
    gl_Position = pc.view_projection * pc.model * vec4(position, 1);
}
