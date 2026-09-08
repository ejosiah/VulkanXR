#version 450

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;

layout(location = 0) out vec3 world_normal;

layout(push_constant) uniform constants {
    mat4 model;
    mat4 view_projection;
} pc;

void main() {
    world_normal = normalize(mat3(transpose(inverse(pc.model))) * normal);
    gl_Position = pc.view_projection * pc.model * vec4(position, 1.0);
}
