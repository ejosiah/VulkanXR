#version 460

layout(binding = 0) uniform sampler2DArray source_image;
layout(push_constant) uniform constants {
    mat4 transform;
    vec4 parameters;
} pc;
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 color;

void main() {
    color = texture(source_image, vec3(uv, pc.parameters.z));
}
