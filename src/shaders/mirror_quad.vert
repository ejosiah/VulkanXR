#version 460

layout(push_constant) uniform constants {
    mat4 transform;
    vec4 parameters;
} pc;
layout(location = 0) out vec2 uv;

const vec2 positions[6] = vec2[](vec2(-0.5, -0.5), vec2(0.5, -0.5), vec2(-0.5, 0.5), vec2(-0.5, 0.5), vec2(0.5, -0.5), vec2(0.5, 0.5));

void main() {
    vec2 position = positions[gl_VertexIndex];
    uv = position + 0.5;
    gl_Position = pc.transform * vec4(position * pc.parameters.xy, 0.0, 1.0);
}
