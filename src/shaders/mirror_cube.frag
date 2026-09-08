#version 460

layout(binding = 0) uniform samplerCube source_image;
layout(push_constant) uniform constants {
    mat4 transform;
    vec4 parameters;
} pc;
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 color;

void main() {
    vec4 world = pc.transform * vec4(uv * 2.0 - 1.0, 1.0, 1.0);
    color = texture(source_image, normalize(world.xyz / world.w));
}
