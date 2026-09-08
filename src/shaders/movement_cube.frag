#version 450

layout(location = 0) in vec3 world_normal;
layout(location = 0) out vec4 color;

void main() {
    color = vec4(abs(normalize(world_normal)), 1.0);
}
