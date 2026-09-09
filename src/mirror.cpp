#include <vxr/mirror.hpp>

#include <vxr/openxr_helpers.hpp>
#include <vxr/transforms.hpp>

#include <vulkanizer/barrier.hpp>
#include <vulkanizer/builders.hpp>
#include <vulkanizer/render.hpp>
#include <vulkanizer/status.hpp>

#include <cmrc/cmrc.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <stdexcept>
#include <type_traits>

CMRC_DECLARE(vxr_shaders);

namespace vxr {

    namespace {

        [[nodiscard]] std::string shader_source(const std::string &path) {
            const auto filesystem = cmrc::vxr_shaders::get_filesystem();
            const auto shader = filesystem.open(path);
            return {shader.begin(), shader.end()};
        }

        [[nodiscard]] mat4 mirror_view_projection(const rendering::context &context) {
            if (context.views.empty()) {
                return mat4{1.0F};
            }
            const auto &view = context.views.front();
            return projection(view.fov, 0.05F, 1000.0F) * inverse(to_matrix(view.pose));
        }

        [[nodiscard]] mat4 mirror_inverse_view_projection(const rendering::context &context) {
            return inverse(mirror_view_projection(context));
        }

    } // namespace

    mirror::mirror(GLFWwindow *window, rendering::context &context, mirror_config config) : context_{context}, config_{std::move(config)}, app_{window, context.vulkan, config_.vsync} {
    }

    mirror::~mirror() {
        cleanup();
    }

    void mirror::init() {
        swapchain_ = app_.create_swapchain();
        swapchain_views_ = vkz::create_swapchain_image_views(context_.device(), *swapchain_);
        command_pool_.init(context_.device(), app_.queue_family_index(), VK_COMMAND_POOL_CREATE_TRANSIENT_BIT, app_.graphics_queue());
        descriptor_layout_ =
            vkz::make_descriptor_set_layout_builder(context_.device())
                .name("mirror_source")
                .binding(0)
                    .descriptor_type(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                    .descriptor_count(1)
                    .shader_stages(VK_SHADER_STAGE_FRAGMENT_BIT)
                .create_layout();
        descriptor_pool_ = vkz::descriptor_pool{context_.device(), 64, {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 64}}};
        sampler_ = vkz::sampler::builder(context_.device()).mag_filter(VK_FILTER_LINEAR).min_filter(VK_FILTER_LINEAR).address_mode(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE).build();
        create_pipelines();
        initialized_ = true;
        spdlog::info("Desktop mirror initialized at {}x{}{}", swapchain_->width(), swapchain_->height(), config_.multiview ? " with multiview presentation" : "");
    }

    void mirror::cleanup() noexcept {
        if (!initialized_) {
            return;
        }
        if (context_.device()) {
            vkDeviceWaitIdle(context_.device().logical);
        }
        projection_pipeline_.destroy();
        cube_pipeline_.destroy();
        quad_pipeline_.destroy();
        equirect_pipeline_.destroy();
        sampler_.destroy();
        descriptor_pool_.destroy();
        descriptor_layout_.destroy();
        command_pool_.deinit();
        vkz::destroy_image_views(context_.device().logical, swapchain_views_);
        swapchain_views_.clear();
        swapchain_.reset();
        initialized_ = false;
    }

    void mirror::create_pipelines() {
        projection_pipeline_ = create_pipeline("mirror.vert", "mirror_projection.frag", "mirror_projection");
        cube_pipeline_ = create_pipeline("mirror.vert", "mirror_cube.frag", "mirror_cube");
        quad_pipeline_ = create_pipeline("mirror_quad.vert", "mirror_quad.frag", "mirror_quad");
        equirect_pipeline_ = create_pipeline("mirror.vert", "mirror_equirect.frag", "mirror_equirect");
    }

    vkz::pipeline mirror::create_pipeline(const std::string &vertex_shader, const std::string &fragment_shader, const std::string &name) {
        auto builder = vkz::make_graphics_pipeline_builder(context_.device());
        builder
            .shader_stage()
                .vertex_shader(shader_source(vertex_shader))
                .fragment_shader(shader_source(fragment_shader))
            .vertex_input_state()
            .input_assembly_state()
                .triangles()
            .viewport_state()
                .viewport()
                    .origin(0, 0)
                    .dimension(swapchain_->width(), swapchain_->height())
                    .min_depth(0.0F)
                    .max_depth(1.0F)
                .scissor()
                    .offset(0, 0)
                    .extent(static_cast<std::int32_t>(swapchain_->width()), static_cast<std::int32_t>(swapchain_->height()))
            .rasterization_state()
                .polygon_mode_fill()
                .cull_none()
            .multisample_state()
                .rasterization_samples(VK_SAMPLE_COUNT_1_BIT);

        auto &attachment = builder.color_blend_state().attachment();
        if (context_.blend_mode == XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND) {
            attachment
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
                .add();
        } else if (context_.blend_mode == XR_ENVIRONMENT_BLEND_MODE_ADDITIVE) {
            attachment
                .enable_blend()
                .src_color_blend_factor()
                    .one()
                .dst_color_blend_factor()
                    .one()
                .color_blend_op()
                    .add()
                .src_alpha_blend_factor()
                    .one()
                .dst_alpha_blend_factor()
                    .one()
                .alpha_blend_op()
                    .add()
                .add();
        } else {
            attachment.disable_blend().add();
        }

        return builder
            .layout()
                .add_descriptor_set_layout(descriptor_layout_)
                .add_push_constant_range(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push_constants))
            .dynamic_render_pass()
                .add_color_attachment(swapchain_->format())
            .name(name)
            .build();
    }

    void mirror::render(const layers &composition_layers, const std::map<XrSwapchain, std::uint32_t> &image_indices) {
        if (composition_layers.empty() || should_close()) {
            return;
        }
        if (!initialized_) {
            init();
        }

        app_.wait_for_drawable_window();
        std::uint32_t swapchain_image_index{};
        VkFence acquire_fence{};
        const VkFenceCreateInfo fence_info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        VKZ_CHECK_VULKAN(vkCreateFence(context_.device().logical, &fence_info, nullptr, &acquire_fence));
        const VkResult acquire_result = vkAcquireNextImageKHR(context_.device().logical, swapchain_->handle(), UINT64_MAX, VK_NULL_HANDLE, acquire_fence, &swapchain_image_index);
        if (acquire_result == VK_ERROR_OUT_OF_DATE_KHR) {
            vkDestroyFence(context_.device().logical, acquire_fence, nullptr);
            return;
        }
        VKZ_CHECK_VULKAN(acquire_result);
        VKZ_CHECK_VULKAN(vkWaitForFences(context_.device().logical, 1, &acquire_fence, VK_TRUE, UINT64_MAX));
        vkDestroyFence(context_.device().logical, acquire_fence, nullptr);

        const auto command_buffer = command_pool_.create_command_buffer();
        const VkImageSubresourceRange destination_range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkImage destination = swapchain_->get_image(swapchain_image_index);
        vkz::barrier::push_and_flush(command_buffer, destination, destination_range, VK_PIPELINE_STAGE_2_NONE, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_NONE,
                                     VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

        vkz::render_info render_info;
        render_info.color_attachments.push_back({swapchain_views_[swapchain_image_index], swapchain_->format(), {0.0F, 0.0F, 0.0F, 1.0F}});
        render_info.render_area = {swapchain_->width(), swapchain_->height()};
        std::vector<vkz::image_view> temporary_views;
        vkz::render(command_buffer, render_info, [&] {
            for (const auto &composition_layer : composition_layers) {
                draw_layer(command_buffer, composition_layer, image_indices, temporary_views);
            }
        });

        vkz::barrier::push_and_flush(command_buffer, destination, destination_range, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_2_NONE,
                                     VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
        VKZ_CHECK_VULKAN(vkEndCommandBuffer(command_buffer));
        command_pool_.submit_and_wait(command_buffer);
        command_pool_.destroy(command_buffer);

        const VkSwapchainKHR handle = swapchain_->handle();
        VkPresentInfoKHR present_info{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        present_info.swapchainCount = 1;
        present_info.pSwapchains = &handle;
        present_info.pImageIndices = &swapchain_image_index;
        const VkResult present_result = vkQueuePresentKHR(app_.graphics_queue(), &present_info);
        if (present_result != VK_ERROR_OUT_OF_DATE_KHR && present_result != VK_SUBOPTIMAL_KHR) {
            VKZ_CHECK_VULKAN(present_result);
        }

        for (auto &view : temporary_views) {
            view.destroy();
        }
        descriptor_pool_.reset();
    }

    void mirror::draw_layer(VkCommandBuffer command_buffer, const layer &composition_layer, const std::map<XrSwapchain, std::uint32_t> &image_indices,
                            std::vector<vkz::image_view> &temporary_views) {
        std::visit(
            [&](auto *value) {
                using layer_type = std::remove_cv_t<std::remove_pointer_t<decltype(value)>>;
                if (!value) {
                    return;
                }
                if constexpr (std::is_same_v<layer_type, XrCompositionLayerProjection>) {
                    draw_projection(command_buffer, *value, image_indices, temporary_views);
                } else if constexpr (std::is_same_v<layer_type, XrCompositionLayerCubeKHR>) {
                    draw_cube(command_buffer, *value, image_indices, temporary_views);
                } else if constexpr (std::is_same_v<layer_type, XrCompositionLayerQuad>) {
                    draw_quad(command_buffer, *value, image_indices, temporary_views);
                } else if constexpr (std::is_same_v<layer_type, XrCompositionLayerEquirectKHR>) {
                    draw_equirect(command_buffer, *value, image_indices, temporary_views);
                }
            },
            composition_layer.value);
    }

    void mirror::draw_projection(VkCommandBuffer command_buffer, const XrCompositionLayerProjection &layer, const std::map<XrSwapchain, std::uint32_t> &image_indices,
                                 std::vector<vkz::image_view> &temporary_views) {
        if (layer.viewCount == 0 || !layer.views) {
            return;
        }
        const auto handle = layer.views[0].subImage.swapchain;
        push_constants constants;
        constants.parameters = {config_.eye_separation, config_.barrel_distortion, static_cast<float>(layer.viewCount), config_.multiview ? 1.0F : 0.0F};
        draw_texture(command_buffer, projection_pipeline_, handle, image_index(handle, image_indices), VK_IMAGE_VIEW_TYPE_2D_ARRAY, 0, layer.viewCount, constants, temporary_views);
    }

    void mirror::draw_cube(VkCommandBuffer command_buffer, const XrCompositionLayerCubeKHR &layer, const std::map<XrSwapchain, std::uint32_t> &image_indices,
                           std::vector<vkz::image_view> &temporary_views) {
        push_constants constants;
        pose orientation;
        orientation.orientation = {layer.orientation.x, layer.orientation.y, layer.orientation.z, layer.orientation.w};
        constants.transform = mirror_inverse_view_projection(context_) * to_matrix(orientation);
        draw_texture(command_buffer, cube_pipeline_, layer.swapchain, image_index(layer.swapchain, image_indices), VK_IMAGE_VIEW_TYPE_CUBE, 0, 6, constants, temporary_views);
    }

    void mirror::draw_quad(VkCommandBuffer command_buffer, const XrCompositionLayerQuad &layer, const std::map<XrSwapchain, std::uint32_t> &image_indices,
                           std::vector<vkz::image_view> &temporary_views) {
        push_constants constants;
        constants.transform = mirror_view_projection(context_) * to_matrix(layer.pose);
        constants.parameters = {layer.size.width, layer.size.height, 0.0F, 0.0F};
        draw_texture(command_buffer, quad_pipeline_, layer.subImage.swapchain, image_index(layer.subImage.swapchain, image_indices), VK_IMAGE_VIEW_TYPE_2D_ARRAY,
                     layer.subImage.imageArrayIndex, 1, constants, temporary_views);
    }

    void mirror::draw_equirect(VkCommandBuffer command_buffer, const XrCompositionLayerEquirectKHR &layer, const std::map<XrSwapchain, std::uint32_t> &image_indices,
                               std::vector<vkz::image_view> &temporary_views) {
        push_constants constants;
        constants.transform = mirror_inverse_view_projection(context_) * to_matrix(layer.pose);
        constants.parameters = {layer.scale.x, layer.bias.x, 0.0F, 0.0F};
        draw_texture(command_buffer, equirect_pipeline_, layer.subImage.swapchain, image_index(layer.subImage.swapchain, image_indices), VK_IMAGE_VIEW_TYPE_2D_ARRAY,
                     layer.subImage.imageArrayIndex, 1, constants, temporary_views);
    }

    void mirror::draw_texture(VkCommandBuffer command_buffer, vkz::pipeline &pipeline, XrSwapchain handle, std::uint32_t source_image_index, VkImageViewType view_type,
                              std::uint32_t base_layer, std::uint32_t layer_count, const push_constants &constants, std::vector<vkz::image_view> &temporary_views) {
        auto &source_swapchain = context_.swapchains.at(std::ranges::find_if(context_.swapchains, [handle](const auto &entry) {
            return entry.second.handle() == handle;
        })->first);
        vkz::image image;
        image.handle = source_swapchain.images()[source_image_index].image;
        image.create_info = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        image.create_info.format = source_swapchain.format();
        image.create_info.arrayLayers = source_swapchain.create_info().array_size_;
        image.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        auto view =
            vkz::image_view::builder(context_.device())
                .image(image)
                .view_type(view_type)
                .format(source_swapchain.format())
                .aspect_mask(VK_IMAGE_ASPECT_COLOR_BIT)
                .base_array_layer(base_layer)
                .layer_count(layer_count)
                .build();
        temporary_views.push_back(view);

        const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, base_layer, layer_count};
        vkz::barrier::push_and_flush(command_buffer, image.handle, range, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                                     VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_2_SHADER_READ_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        const auto descriptor_set = descriptor_pool_.allocate(descriptor_layout_);
        vkz::update_descriptor(context_.device(), {.descriptor_set = descriptor_set, .bindings = {vkz::texture_descriptor{view, sampler_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0}}});
        pipeline.descriptor_sets = {descriptor_set};
        vkz::bind_pipeline(command_buffer, pipeline);
        vkz::push_constants(command_buffer, pipeline, constants);
        vkz::draw(command_buffer, pipeline.handle == quad_pipeline_.handle ? 6 : 3);
        vkz::barrier::push_and_flush(command_buffer, image.handle, range, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                                     VK_ACCESS_2_SHADER_READ_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    }

    std::uint32_t mirror::image_index(XrSwapchain handle, const std::map<XrSwapchain, std::uint32_t> &image_indices) const {
        const auto found = image_indices.find(handle);
        return found == image_indices.end() ? 0 : found->second;
    }

    bool mirror::should_close() const {
        return app_.should_close();
    }

    void mirror::poll_events() const {
        app_.poll_events();
    }

} // namespace vxr
