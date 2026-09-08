#version 460

layout(binding = 0) uniform sampler2DArray source_image;
layout(push_constant) uniform constants {
    mat4 transform;
    vec4 parameters;
} pc;
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 color;

const float pi = 3.14159265358979323846;

void main() {
    vec4 world = pc.transform * vec4(uv * 2.0 - 1.0, 1.0, 1.0);
    vec3 direction = normalize(world.xyz / world.w);
    vec2 equirect_uv = vec2(atan(direction.x, -direction.z) / (2.0 * pi) + 0.5, asin(clamp(direction.y, -1.0, 1.0)) / pi + 0.5);
    equirect_uv.x = equirect_uv.x * pc.parameters.x + pc.parameters.y;
    color = texture(source_image, vec3(equirect_uv, pc.parameters.z));
}
