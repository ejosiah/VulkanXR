#include <vxr/font_pipeline.hpp>

#include <vulkanizer/builders.hpp>
#include <vulkanizer/commands.hpp>

namespace vxr {

    font_pipeline font_pipeline::create(const font_pipeline_create_info &create_info) {
        font_pipeline result;
        result.pipeline_ =
            vkz::make_graphics_pipeline_builder(create_info.device)
                .shader_stage()
                    .vertex_shader(create_info.vertex_shader)
                    .fragment_shader(create_info.fragment_shader)
                .vertex_input_state()
                .input_assembly_state()
                    .triangles()
                .viewport_state()
                    .viewport()
                        .origin(0, 0)
                        .dimension(create_info.extent.width, create_info.extent.height)
                        .min_depth(0.0F)
                        .max_depth(1.0F)
                    .scissor()
                        .offset(0, 0)
                        .extent(static_cast<std::int32_t>(create_info.extent.width), static_cast<std::int32_t>(create_info.extent.height))
                .rasterization_state()
                    .polygon_mode_fill()
                    .cull_none()
                .multisample_state()
                    .rasterization_samples(VK_SAMPLE_COUNT_1_BIT)
                .color_blend_state()
                    .attachment()
                        .enable_blend()
                        .src_color_blend_factor()
                            .src_alpha()
                        .dst_color_blend_factor()
                            .one_minus_src_alpha()
                        .color_blend_op()
                            .add()
                        .src_alpha_blend_factor()
                            .one()
                        .dst_alpha_blend_factor()
                            .one_minus_src_alpha()
                        .alpha_blend_op()
                            .add()
                        .add()
                .layout()
                    .add_descriptor_set_layout(create_info.descriptor_layout)
                    .add_push_constant_range(VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(font_push_constants))
                .dynamic_render_pass()
                    .add_color_attachment(create_info.color_format)
                .name(create_info.name)
                .build();
        return result;
    }

    void font_pipeline::destroy() noexcept {
        pipeline_.destroy();
        pipeline_ = {};
    }

    void font_pipeline::bind(VkCommandBuffer command_buffer, vkz::descriptor_set descriptor_set, const font_push_constants &constants) {
        pipeline_.descriptor_sets = {descriptor_set};
        vkz::bind_pipeline(command_buffer, pipeline_);
        vkz::push_constants(command_buffer, pipeline_, constants);
    }

} // namespace vxr
