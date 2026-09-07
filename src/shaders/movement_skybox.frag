#version 450

layout(location = 0) in vec2 ndc;
layout(location = 0) out vec4 color;

layout(binding = 0) uniform samplerCube skybox;

layout(push_constant) uniform constants {
    mat4 inverse_view_projection;
    vec4 origin;
    mat4 view_projection;
    vec4 teleport_origin;
    vec4 teleport_velocity;
    vec4 teleport_target;
    uint teleport_active;
} pc;

void main() {
    const vec4 near_point = pc.inverse_view_projection * vec4(ndc, 0.0, 1.0);
    const vec4 far_point = pc.inverse_view_projection * vec4(ndc, 1.0, 1.0);
    const vec3 direction = normalize(far_point.xyz / far_point.w - near_point.xyz / near_point.w);
    color = texture(skybox, direction);
}
