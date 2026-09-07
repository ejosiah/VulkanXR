#version 460
#extension GL_GOOGLE_include_directive : require

#include "hash.glsl"

layout(location = 0) in vec2 ndc;
layout(location = 0) out vec4 frag_color;

layout(binding = 1) uniform sampler2D grid_texture;
layout(binding = 2) uniform samplerCube irradiance_texture;
layout(binding = 3) uniform samplerCube specular_texture;

layout(push_constant) uniform constants {
    mat4 inverse_view_projection;
    vec4 origin;
    mat4 view_projection;
    vec4 teleport_origin;
    vec4 teleport_velocity;
    vec4 teleport_target;
    uint teleport_active;
} pc;

const float major_spacing = 1.0;
const float minor_spacing = 0.1;
const float metalness = 0.8;
const float roughness = 0.3;

const vec2 jitter[8] = vec2[](
    vec2(-0.375, -0.125),
    vec2(0.125, -0.375),
    vec2(0.375, 0.125),
    vec2(-0.125, 0.375),
    vec2(-0.125, -0.375),
    vec2(0.375, -0.125),
    vec2(0.125, 0.375),
    vec2(-0.375, 0.125)
);

bool sample_floor(vec2 sample_ndc, out vec3 sample_color) {
    const vec4 near_point = pc.inverse_view_projection * vec4(sample_ndc, 0.0, 1.0);
    const vec4 far_point = pc.inverse_view_projection * vec4(sample_ndc, 1.0, 1.0);
    const vec3 view_dir = normalize(far_point.xyz / far_point.w - near_point.xyz / near_point.w);
    const vec3 origin = pc.origin.xyz;

    if (view_dir.y >= 0.0) {
        return false;
    }

    const float t = -origin.y / view_dir.y;
    if (t <= 0.0) {
        return false;
    }

    const vec3 p = origin + view_dir * t;
    const vec2 uv_major = fract(p.xz / major_spacing);
    const float grid_major = 1.0 - textureLod(grid_texture, uv_major, 0.0).r;

    const vec2 uv_minor = fract(p.xz / minor_spacing);
    const vec2 id_minor = floor(p.xz / minor_spacing);
    const float grid_minor = 1.0 - textureLod(grid_texture, uv_minor, 0.0).r;

    const float h = hash12(id_minor + 1.0);
    const float c = grid_major * h * grid_minor;
    const vec3 base_color = vec3(c);
    const vec3 normal = vec3(0.0, 1.0, 0.0);
    const vec3 view = normalize(origin - p);
    const vec3 reflection = reflect(-view, normal);
    const vec3 f0 = mix(vec3(0.04), base_color, metalness);
    const float n_dot_v = max(dot(normal, view), 0.0);
    const vec3 fresnel = f0 + (1.0 - f0) * pow(1.0 - n_dot_v, 5.0);
    const vec3 diffuse = textureLod(irradiance_texture, normal, 0.0).rgb * base_color * (1.0 - metalness);
    const vec3 specular = textureLod(specular_texture, reflection, roughness * 7.0).rgb * fresnel;
    const vec3 lighting = diffuse + specular;
    sample_color = lighting / (lighting + vec3(1.0));
    if (pc.teleport_active != 0) {
        const float ring = abs(length(p.xz - pc.teleport_target.xz) - 0.25);
        if (ring < 0.015) {
            sample_color = vec3(0.0, 1.0, 1.0);
        }
    }
    return true;
}

float distance_to_segment(vec2 point, vec2 start, vec2 end) {
    const vec2 segment = end - start;
    const float length_squared = dot(segment, segment);
    if (length_squared == 0.0) {
        return length(point - start);
    }
    const float distance_along_segment = clamp(dot(point - start, segment) / length_squared, 0.0, 1.0);
    return length(point - (start + segment * distance_along_segment));
}

float teleport_ray_distance(vec2 point) {
    const float half_gravity = -4.905;
    const float discriminant = pc.teleport_velocity.y * pc.teleport_velocity.y - 4.0 * half_gravity * pc.teleport_origin.y;
    const float root = sqrt(max(discriminant, 0.0));
    const float first = (-pc.teleport_velocity.y + root) / (2.0 * half_gravity);
    const float second = (-pc.teleport_velocity.y - root) / (2.0 * half_gravity);
    const float end_time = first > 0.0 ? first : second;
    float closest = 1000.0;

    for (int segment_index = 0; segment_index < 64; ++segment_index) {
        if ((segment_index / 4) % 2 != 0) continue;
        const float start_time = end_time * float(segment_index) / 64.0;
        const float finish_time = end_time * float(segment_index + 1) / 64.0;
        const vec3 start_position = pc.teleport_origin.xyz + pc.teleport_velocity.xyz * start_time + vec3(0.0, half_gravity * start_time * start_time, 0.0);
        const vec3 finish_position = pc.teleport_origin.xyz + pc.teleport_velocity.xyz * finish_time + vec3(0.0, half_gravity * finish_time * finish_time, 0.0);
        const vec4 start_clip = pc.view_projection * vec4(start_position, 1.0);
        const vec4 finish_clip = pc.view_projection * vec4(finish_position, 1.0);
        if (start_clip.w <= 0.0 || finish_clip.w <= 0.0) continue;
        closest = min(closest, distance_to_segment(point, start_clip.xy / start_clip.w, finish_clip.xy / finish_clip.w));
    }
    return closest;
}

void main() {
    const vec2 pixel_size = vec2(length(dFdx(ndc)), length(dFdy(ndc)));
    vec3 color = vec3(0.0);
    float coverage = 0.0;

    for (int sample_index = 0; sample_index < 8; ++sample_index) {
        vec3 sample_color;
        if (sample_floor(ndc + jitter[sample_index] * pixel_size, sample_color)) {
            color += sample_color;
            coverage += 1.0;
        }
    }

    const bool ray_visible = pc.teleport_active != 0 && teleport_ray_distance(ndc) < max(pixel_size.x, pixel_size.y) * 2.0;
    if (coverage == 0.0) {
        if (ray_visible) {
            frag_color = vec4(0.0, 1.0, 1.0, 1.0);
            return;
        }
        discard;
    }

    frag_color = vec4(color / coverage, coverage / 8.0);
    if (ray_visible) {
        frag_color = vec4(0.0, 1.0, 1.0, 1.0);
    }
}
