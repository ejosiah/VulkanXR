#version 460

layout(binding = 0) uniform sampler2DArray source_image;
layout(push_constant) uniform constants {
    mat4 transform;
    vec4 parameters;
} pc;
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 color;

void main() {
    float view = uv.x < 0.5 ? 0.0 : 1.0;
    vec2 eye_uv = vec2(fract(uv.x * 2.0), uv.y);
    vec2 centered = eye_uv * 2.0 - 1.0;
    vec2 distorted = centered / (1.0 - pc.parameters.y * length(centered));
    vec2 source_uv = (distorted + 1.0) * 0.5;
    source_uv.x += view == 0.0 ? pc.parameters.x * 0.5 : -pc.parameters.x * 0.5;
    bool inside = source_uv.x >= 0.0 && source_uv.x <= 1.0 && source_uv.y >= 0.0 && source_uv.y <= 1.0;
    color = inside ? texture(source_image, vec3(source_uv, view)) : vec4(0.0);
}
