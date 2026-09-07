#include "space_visualization_renderer.hpp"

#include <vxr/vxr.hpp>
#include <vxr/openxr_helpers.hpp>

#include <vulkanizer/builders.hpp>
#include <vulkanizer/barrier.hpp>
#include <vulkanizer/commands.hpp>
#include <vulkanizer/primitives.hpp>
#include <vulkanizer/render.hpp>
#include <vulkanizer/status.hpp>

#include <cmrc/cmrc.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string_view>
#include <utility>

CMRC_DECLARE(vxr_shaders);

namespace {

    constexpr VkFormat color_format = VK_FORMAT_R8G8B8A8_SRGB;
    constexpr VkFormat depth_format = VK_FORMAT_D32_SFLOAT;

    [[nodiscard]] std::string shader_source(const std::string &path) {
        const auto filesystem = cmrc::vxr_shaders::get_filesystem();
        const auto shader = filesystem.open(path);
        return {shader.begin(), shader.end()};
    }

    [[nodiscard]] std::optional<XrReferenceSpaceType> reference_space(std::string_view name) {
        if (name == "stage") {
            return XR_REFERENCE_SPACE_TYPE_STAGE;
        }
        if (name == "local") {
            return XR_REFERENCE_SPACE_TYPE_LOCAL;
        }
        if (name == "local-floor") {
            return XR_REFERENCE_SPACE_TYPE_LOCAL_FLOOR_EXT;
        }
        if (name == "view") {
            return XR_REFERENCE_SPACE_TYPE_VIEW;
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<XrReferenceSpaceType> base_space_from_arguments(int argc, char **argv) {
        constexpr std::string_view option = "--base-space";
        for (int index = 1; index < argc; ++index) {
            const std::string_view argument = argv[index];
            std::string_view value;
            if (argument == option) {
                if (++index >= argc) {
                    spdlog::error("--base-space requires one of: stage, local, local-floor, view");
                    return std::nullopt;
                }
                value = argv[index];
            } else if (argument.starts_with("--base-space=")) {
                value = argument.substr(option.size() + 1);
            } else {
                spdlog::error("Unknown argument '{}'. Expected --base-space stage|local|local-floor|view", argument);
                return std::nullopt;
            }

            const auto space = reference_space(value);
            if (!space) {
                spdlog::error("Unknown base space '{}'. Expected stage, local, local-floor, or view", value);
                return std::nullopt;
            }
            return space;
        }
        return XR_REFERENCE_SPACE_TYPE_STAGE;
    }

} // namespace

void space_visualization_renderer::init() {
    allocator_ = vkz::vma_memory_allocator::create(vulkan_context());
    command_pool_.init(device(), graphics_queue_family(), VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, graphics_queue());
    create_cube();
    create_image_views();
    create_pipeline();
    setup_projection_layer();
}

void space_visualization_renderer::cleanup() {
    if (!device()) {
        return;
    }
    vkDeviceWaitIdle(device().logical);
    command_pool_.deinit();
    pipeline_.destroy();
    for (auto &views : color_views_) {
        for (auto &view : views) {
            view.destroy();
        }
        views.clear();
    }
    for (auto &views : depth_views_) {
        for (auto &view : views) {
            view.destroy();
        }
        views.clear();
    }
    for (auto &image : owned_depth_images_) {
        image.destroy();
    }
    vertex_buffer_.destroy();
    index_buffer_.destroy();
    allocator_.destroy();
}

void space_visualization_renderer::begin_frame() {
    cubes_.clear();
}

void space_visualization_renderer::set(const std::vector<vxr::space_location> &space_locations) {
    for (const auto &space_location : space_locations) {
        cubes_.push_back({{space_location.value, glm::vec3{0.25F}}});
    }
}

std::vector<vxr::vibration> space_visualization_renderer::set(const vxr::action_set &actions) {
    std::vector<vxr::vibration> vibrations;
    const auto update_squeeze = [&](const char *action_name, const char *haptic_action, std::size_t hand) {
        const auto &squeeze = actions.get(action_name);
        if (!squeeze.active) {
            return;
        }
        const float value = squeeze.value<float>();
        hand_scale_[hand] = 1.0F - 0.5F * value;
        if (value > 0.9F) {
            vibrations.push_back({haptic_action});
        }
    };
    update_squeeze("left_hand_squeeze", "vibrate_left", 0);
    update_squeeze("right_hand_squeeze", "vibrate_right", 1);

    const auto add_hand = [&](const char *action_name, std::size_t hand) {
        const auto &hand_pose = actions.get(action_name);
        if (hand_pose.active) {
            cubes_.push_back({{hand_pose.value<vxr::pose>(), glm::vec3{0.1F} * hand_scale_[hand]}});
        }
    };
    add_hand("left_hand_pose", 0);
    add_hand("right_hand_pose", 1);
    return vibrations;
}

void space_visualization_renderer::paused(const vxr::frame_info &frame, vxr::layers &layers) {
    render(frame, layers);
}

void space_visualization_renderer::render(const vxr::frame_info &frame, vxr::layers &layers) {
    const auto view_count = std::min(frame.view.views.size(), projection_views_.size());
    for (std::uint32_t view_index = 0; view_index < view_count; ++view_index) {
        render_view(frame, view_index);
        projection_views_[view_index].pose = frame.view.views[view_index].pose;
        projection_views_[view_index].fov = frame.view.views[view_index].fov;
    }
    projection_layer_.viewCount = static_cast<std::uint32_t>(view_count);
    layers.push_back({&projection_layer_, 0});
}

const char *space_visualization_renderer::name() const noexcept {
    return "Space Visualization";
}

std::unique_ptr<vxr::renderer> space_visualization_renderer::create(vxr::context &xr_context, vxr::session::context &session_context) {
    return std::make_unique<space_visualization_renderer>(xr_context, session_context);
}

vxr::session::config space_visualization_renderer::session_config() {
    vxr::session::config config;
    config
        .stage()
        .add_space(
            vxr::session::reference_space_specification{}
                .name("view_front")
                .view()
                .translate(0.0F, 0.0F, -2.0F)
        )
        .add_space(
            vxr::session::reference_space_specification{}
                .name("local")
                .local()
        )
        .add_space(
            vxr::session::reference_space_specification{}
                .name("stage")
                .stage()
        )
        .add_space(
            vxr::session::reference_space_specification{}
                .name("stage_left")
                .stage()
                .translate(-2.0F, 0.0F, -2.0F)
        )
        .add_space(
            vxr::session::reference_space_specification{}
                .name("stage_right")
                .stage()
                .translate(2.0F, 0.0F, -2.0F)
        )
        .add_space(
            vxr::session::reference_space_specification{}
                .name("stage_left_rotated")
                .stage()
                .translate(-2.0F, 0.5F, -2.0F)
                .rotate_y(60.0F)
        )
        .add_space(
            vxr::session::reference_space_specification{}
                .name("stage_right_rotated")
                .stage()
                .translate(2.0F, 0.5F, -2.0F)
                .rotate_y(-60.0F)
        )
        .add_action_set(
            vxr::session::action_set_specification{}
                .name("main_action_set")
                .description("Main action set")
                .add_action("left_hand_pose", vxr::input::location::left_hand, vxr::input::identifier::grip, vxr::input::component::pose, "Left hand grip")
                .add_action("right_hand_pose", vxr::input::location::right_hand, vxr::input::identifier::grip, vxr::input::component::pose, "Right hand grip")
                .add_action("left_hand_squeeze", vxr::input::location::left_hand, vxr::input::identifier::squeeze, vxr::input::component::value, "Left hand squeeze")
                .add_action("right_hand_squeeze", vxr::input::location::right_hand, vxr::input::identifier::squeeze, vxr::input::component::value, "Right hand squeeze")
                .add_action("vibrate_left", vxr::input::location::left_hand, vxr::output::identifier::haptic, vxr::output::component::vibrate, "Vibrate left hand")
                .add_action("vibrate_right", vxr::input::location::right_hand, vxr::output::identifier::haptic, vxr::output::component::vibrate, "Vibrate right hand")
        )
        .add_swapchain(
            vxr::swapchain_create_info{}
                .name("main_depth")
                .usage()
                    .depth_stencil_attachment()
                .format(depth_format)
                .extent(2064, 2096)
                .array_size(2)
                .render(false)
        )
        .add_swapchain(
            vxr::swapchain_create_info{}
                .name("main")
                .usage()
                    .color_attachment()
                .format(color_format)
                .extent(2064, 2096)
                .array_size(2)
                .depth("main_depth")
        );
    return config;
}

void space_visualization_renderer::create_cube() {
    const auto cube = vkz::prim::cube();
    vertex_buffer_ =
        vkz::buffer::builder(allocator_)
            .usage(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)
            .memory_usage(VMA_MEMORY_USAGE_CPU_TO_GPU)
            .size(VKZ_BYTE_SIZE(cube.vertices))
            .build();
    auto vertex_mapping = vertex_buffer_.map();
    std::memcpy(vertex_mapping._, cube.vertices.data(), static_cast<std::size_t>(vertex_buffer_.create_info.size));
    vertex_mapping.unmap();

    index_buffer_ =
        vkz::buffer::builder(allocator_)
            .usage(VK_BUFFER_USAGE_INDEX_BUFFER_BIT)
            .memory_usage(VMA_MEMORY_USAGE_CPU_TO_GPU)
            .size(VKZ_BYTE_SIZE(cube.indices))
            .build();
    auto index_mapping = index_buffer_.map();
    std::memcpy(index_mapping._, cube.indices.data(), static_cast<std::size_t>(index_buffer_.create_info.size));
    index_mapping.unmap();
    index_count_ = static_cast<std::uint32_t>(cube.indices.size());
}

void space_visualization_renderer::create_image_views() {
    auto &color_swapchain = get_swapchain("main");
    for (std::uint32_t view_index = 0; view_index < 2; ++view_index) {
        for (const auto &swapchain_image : color_swapchain.images()) {
            vkz::image image{};
            image.handle = swapchain_image.image;
            image.create_info = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
            image.create_info.format = color_format;
            color_views_[view_index].push_back(
                vkz::image_view::builder(device())
                    .image(image)
                    .view_type(VK_IMAGE_VIEW_TYPE_2D)
                    .format(color_format)
                    .aspect_mask(VK_IMAGE_ASPECT_COLOR_BIT)
                    .base_array_layer(view_index)
                    .layer_count(1)
                    .build()
            );
        }
    }

    if (session_context_.depth_supported) {
        auto &depth_swapchain = get_swapchain("main_depth");
        for (std::uint32_t view_index = 0; view_index < 2; ++view_index) {
            for (const auto &swapchain_image : depth_swapchain.images()) {
                vkz::image image{};
                image.handle = swapchain_image.image;
                image.create_info = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
                image.create_info.format = depth_format;
                depth_views_[view_index].push_back(
                    vkz::image_view::builder(device())
                        .image(image)
                        .view_type(VK_IMAGE_VIEW_TYPE_2D)
                        .format(depth_format)
                        .aspect_mask(VK_IMAGE_ASPECT_DEPTH_BIT)
                        .base_array_layer(view_index)
                        .layer_count(1)
                        .build()
                );
            }
        }
        return;
    }

    const auto &create_info = color_swapchain.create_info();
    for (std::uint32_t view_index = 0; view_index < 2; ++view_index) {
        owned_depth_images_[view_index] =
            vkz::image::builder(allocator_)
                .format(depth_format)
                .extent(create_info.width_, create_info.height_)
                .usage(VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
                .build();
        depth_views_[view_index].push_back(
            vkz::image_view::builder(device())
                .image(owned_depth_images_[view_index])
                .format(depth_format)
                .aspect_mask(VK_IMAGE_ASPECT_DEPTH_BIT)
                .build()
        );
    }
}

void space_visualization_renderer::create_pipeline() {
    pipeline_ =
        vkz::make_graphics_pipeline_builder(device())
            .shader_stage()
                .vertex_shader(shader_source("space_visualization.vert"))
                .fragment_shader(shader_source("space_visualization.frag"))
            .vertex_input_state()
                .add_vertex_binding_description(0, sizeof(vkz::prim::vertex), VK_VERTEX_INPUT_RATE_VERTEX)
                .add_vertex_attribute_description(0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(vkz::prim::vertex, position))
                .add_vertex_attribute_description(1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(vkz::prim::vertex, normal))
            .input_assembly_state()
                .triangles()
            .viewport_state()
                .viewport()
                    .origin(0, 0)
                    .dimension(get_swapchain("main").width(), get_swapchain("main").height())
                    .min_depth(0.0F)
                    .max_depth(1.0F)
                .scissor()
                    .offset(0, 0)
                    .extent(static_cast<std::int32_t>(get_swapchain("main").width()), static_cast<std::int32_t>(get_swapchain("main").height()))
            .rasterization_state()
                .polygon_mode_fill()
                .cull_back_face()
                .front_face_counter_clockwise()
            .multisample_state()
                .rasterization_samples(VK_SAMPLE_COUNT_1_BIT)
            .depth_stencil_state()
                .enable_depth_test()
                .enable_depth_write()
                .compare_op_less()
            .color_blend_state()
                .attachment()
                    .add()
            .layout()
                .add_push_constant_range(VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push_constants))
            .dynamic_render_pass()
                .add_color_attachment(color_format)
                .depth_attachment(depth_format)
            .name("space_visualization")
        .build();
}

void space_visualization_renderer::setup_projection_layer() {
    auto &color_swapchain = get_swapchain("main");
    projection_layer_ = vxr::make_struct<XrCompositionLayerProjection>();
    projection_layer_.views = projection_views_.data();
    for (std::uint32_t view_index = 0; view_index < projection_views_.size(); ++view_index) {
        auto &view = projection_views_[view_index];
        view = vxr::make_struct<XrCompositionLayerProjectionView>();
        view.subImage.swapchain = color_swapchain.handle();
        view.subImage.imageRect.extent = {static_cast<std::int32_t>(color_swapchain.width()), static_cast<std::int32_t>(color_swapchain.height())};
        view.subImage.imageArrayIndex = view_index;
        if (session_context_.depth_supported) {
            auto &depth = depth_infos_[view_index];
            depth = vxr::make_struct<XrCompositionLayerDepthInfoKHR>();
            depth.subImage.swapchain = get_swapchain("main_depth").handle();
            depth.subImage.imageRect = view.subImage.imageRect;
            depth.subImage.imageArrayIndex = view_index;
            depth.minDepth = 0.0F;
            depth.maxDepth = 1.0F;
            depth.nearZ = 0.05F;
            depth.farZ = 100.0F;
            view.next = &depth;
        }
    }
}

void space_visualization_renderer::render_view(const vxr::frame_info &frame, std::uint32_t view_index) {
    const auto depth_image = frame.depth_image ? frame.depth_image->image_index : 0;
    const auto command_buffer = command_pool_.create_command_buffer();
    auto color_image = get_swapchain("main").images()[frame.image.image_index].image;
    auto depth_image_handle = session_context_.depth_supported ? get_swapchain("main_depth").images()[depth_image].image : owned_depth_images_[view_index].handle;
    const VkImageSubresourceRange color_range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, view_index, 1};
    const VkImageSubresourceRange depth_range{VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, session_context_.depth_supported ? view_index : 0, 1};
    vkz::barrier::push_and_flush(command_buffer, color_image, color_range, VK_PIPELINE_STAGE_2_NONE, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_NONE,
                                 VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    vkz::barrier::push_and_flush(command_buffer, depth_image_handle, depth_range, VK_PIPELINE_STAGE_2_NONE, VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                                 VK_ACCESS_2_NONE, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                                 VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

    vkz::render_info render_info;
    render_info.color_attachments.push_back({color_views_[view_index].at(frame.image.image_index), color_format, {0.184313729F, 0.309803933F, 0.309803933F, 1.0F}});
    render_info.depth_attachment = vkz::depth_stencil_attachment{depth_views_[view_index].at(depth_image), depth_format};
    render_info.render_area = {get_swapchain("main").width(), get_swapchain("main").height()};
    vkz::render(command_buffer, render_info, [&] {
        vkz::bind_pipeline(command_buffer, pipeline_);
        vkz::bind_vertex_buffer(command_buffer, vertex_buffer_);
        vkz::bind_index_buffer<std::uint32_t>(command_buffer, index_buffer_);

        const auto &view = frame.view.views[view_index];
        const glm::mat4 view_projection = vxr::projection(view.fov, 0.05F, 100.0F) * glm::inverse(vxr::to_matrix(view.pose));
        for (const auto &cube : cubes_) {
            const push_constants constants{static_cast<glm::mat4>(cube.transform), view_projection};
            vkz::push_constants(command_buffer, pipeline_, constants);
            vkz::draw_indexed(command_buffer, index_count_);
        }
    });
    VKZ_CHECK_VULKAN(vkEndCommandBuffer(command_buffer));
    command_pool_.submit_and_wait(command_buffer);
    command_pool_.destroy(command_buffer);
}

int main(int argc, char **argv) {
    spdlog::info("{} space visualization (Vulkan header version {})", vxr::project_name(), vxr::vulkan_header_version());
    const auto base_space = base_space_from_arguments(argc, argv);
    if (!base_space) {
        return EXIT_FAILURE;
    }
    auto context_builder = vxr::context::create();
    context_builder
        .app_name("VulkanXR Space Visualization")
        .vulkan_api_version(VK_API_VERSION_1_2)
        .openxr_api_version(XR_API_VERSION_1_0);
    auto session_config = space_visualization_renderer::session_config().base_space(*base_space);
    vxr::application application{context_builder, session_config, space_visualization_renderer::create};
    application.run();
    return EXIT_SUCCESS;
}
