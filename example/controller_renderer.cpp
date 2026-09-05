#include "controller_renderer.hpp"

#include <vxr/vxr.hpp>
#include <vxr/openxr_helpers.hpp>

#include <vulkanizer/builders.hpp>
#include <vulkanizer/status.hpp>

#include <cmrc/cmrc.hpp>
#include <glm/geometric.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>

CMRC_DECLARE(vxr_shaders);

namespace {

    constexpr VkFormat format = VK_FORMAT_R8G8B8A8_SRGB;
    constexpr std::uint32_t width = 1024;
    constexpr std::uint32_t height = 512;

    [[nodiscard]] std::string shader_source(const std::string &path) {
        const auto filesystem = cmrc::vxr_shaders::get_filesystem();
        const auto shader = filesystem.open(path);
        return {shader.begin(), shader.end()};
    }

    [[nodiscard]] const char *action_type(const vxr::action &action) {
        if (std::holds_alternative<bool>(action.state)) {
            return "XR_ACTION_TYPE_BOOLEAN_INPUT";
        }
        if (std::holds_alternative<float>(action.state)) {
            return "XR_ACTION_TYPE_FLOAT_INPUT";
        }
        if (std::holds_alternative<glm::vec2>(action.state)) {
            return "XR_ACTION_TYPE_VECTOR2F_INPUT";
        }
        return "XR_ACTION_TYPE_POSE_INPUT";
    }

    [[nodiscard]] std::string trackpad_region(const glm::vec2 &position) {
        constexpr float center_radius = 0.35F;
        if (glm::length(position) < center_radius) {
            return "center";
        }
        if (std::abs(position.x) > std::abs(position.y)) {
            return position.x < 0.0F ? "left" : "right";
        }
        return position.y < 0.0F ? "down" : "up";
    }

} // namespace

void controller_renderer::init() {
    allocator_ = vkz::vma_memory_allocator::create(vulkan_context());
    command_pool_.init(device(), graphics_queue_family(), VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, graphics_queue());
    text_buffer_ =
        vkz::buffer::builder(allocator_)
            .usage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)
            .memory_usage(VMA_MEMORY_USAGE_CPU_TO_GPU)
            .size(sizeof(text_buffer))
            .build();
    text_mapping_ = text_buffer_.map();
    descriptor_layout_ =
        vkz::make_descriptor_set_layout_builder(device())
            .name("controller_text")
            .binding(0)
                .descriptor_type(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptor_count(1)
                .shader_stages(VK_SHADER_STAGE_FRAGMENT_BIT)
            .create_layout();
    descriptor_pool_ = vkz::descriptor_pool{device(), 1, {{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}}};
    descriptor_set_ = descriptor_pool_.allocate(descriptor_layout_);
    vkz::update_descriptor(device(), {.descriptor_set = descriptor_set_, .bindings = {vkz::descriptor<vkz::buffer_descriptor>(text_buffer_, 0)}});
    create_render_targets();
    create_pipeline();

    auto &swapchain = get_swapchain("controller_text");
    layer_ = vxr::make_struct<XrCompositionLayerQuad>();
    layer_.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
    layer_.subImage.swapchain = swapchain.handle();
    layer_.subImage.imageRect.extent = {static_cast<std::int32_t>(width), static_cast<std::int32_t>(height)};
    layer_.pose.orientation.w = 1.0F;
    layer_.pose.position = {0.0F, 0.0F, -5.0F};
    layer_.size = {3.2F, 1.6F};
    update_text();
}

void controller_renderer::cleanup() {
    if (!device()) {
        return;
    }
    vkDeviceWaitIdle(device().logical);
    command_pool_.deinit();
    pipeline_.destroy();
    for (const auto framebuffer : framebuffers_) {
        vkDestroyFramebuffer(device().logical, framebuffer, nullptr);
    }
    for (auto &view : image_views_) {
        view.destroy();
    }
    if (render_pass_ != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device().logical, render_pass_, nullptr);
        render_pass_ = VK_NULL_HANDLE;
    }
    descriptor_pool_.destroy();
    descriptor_layout_.destroy();
    text_mapping_.unmap();
    text_buffer_.destroy();
    allocator_.destroy();
}

std::vector<vxr::vibration> controller_renderer::set(const vxr::action_set &actions) {
    for (const auto &[name, action] : actions) {
        if (!action.active || !std::holds_alternative<float>(action.state)) continue;
        const std::size_t hand = name.starts_with("left_") ? 0 : 1;
        if (name.ends_with("trackpad_x")) {
            trackpad_positions_[hand].x = action.value<float>();
        } else if (name.ends_with("trackpad_y")) {
            trackpad_positions_[hand].y = action.value<float>();
        }
    }

    for (const auto &[name, action] : actions) {
        if (!action.active || !action.changed || !std::holds_alternative<bool>(action.state) || !action.value<bool>()) continue;
        const bool left = name.starts_with("left_");
        std::string control;
        if (name.find("trackpad") != std::string::npos) {
            control = "trackpad/" + trackpad_region(trackpad_positions_[left ? 0 : 1]) + "/click";
        } else if (name.find("squeeze") != std::string::npos) {
            control = "squeeze/click";
        } else if (name.find("trigger") != std::string::npos) {
            control = "trigger/click";
        } else {
            control = "menu/click";
        }
        action_history_.push_front(std::string{left ? "/user/hand/left/input/" : "/user/hand/right/input/"} + control + " -> " + action_type(action));
        if (action_history_.size() > 5) {
            action_history_.pop_back();
        }
    }
    update_text();
    return {};
}

void controller_renderer::paused(const vxr::frame_info &frame, vxr::layers &layers) {
    render(frame, layers);
}

void controller_renderer::render(const vxr::frame_info &frame, vxr::layers &layers) {
    draw(frame.image.image_index);
    layers.push_back({&layer_, 0});
}

const char *controller_renderer::name() const noexcept {
    return "Controller Input";
}

std::unique_ptr<vxr::renderer> controller_renderer::create(vxr::context &xr_context, vxr::session::context &session_context) {
    return std::make_unique<controller_renderer>(xr_context, session_context);
}

vxr::session::config controller_renderer::session_config() {
    vxr::session::config config;
    config
        .view()
        .add_action_set(
            vxr::session::action_set_specification{}
                .name("controller")
                .description("Controller input test")
                .add_action("left_trigger_click", vxr::input::location::left_hand, vxr::input::identifier::trigger, vxr::input::component::click, "Left trigger")
                .add_action("right_trigger_click", vxr::input::location::right_hand, vxr::input::identifier::trigger, vxr::input::component::click, "Right trigger")
                .add_action("left_menu_click", vxr::input::location::left_hand, vxr::input::identifier::menu, vxr::input::component::click, "Left menu")
                .add_action("right_menu_click", vxr::input::location::right_hand, vxr::input::identifier::menu, vxr::input::component::click, "Right menu")
                .add_action("left_squeeze_click", vxr::input::location::left_hand, vxr::input::identifier::squeeze, vxr::input::component::click, "Left grip")
                .add_action("right_squeeze_click", vxr::input::location::right_hand, vxr::input::identifier::squeeze, vxr::input::component::click, "Right grip")
                .add_action("left_trackpad_x", vxr::input::location::left_hand, vxr::input::identifier::track_pad, vxr::input::component::x, "Left trackpad X")
                .add_action("left_trackpad_y", vxr::input::location::left_hand, vxr::input::identifier::track_pad, vxr::input::component::y, "Left trackpad Y")
                .add_action("left_trackpad_click", vxr::input::location::left_hand, vxr::input::identifier::track_pad, vxr::input::component::click, "Left trackpad click")
                .add_action("right_trackpad_x", vxr::input::location::right_hand, vxr::input::identifier::track_pad, vxr::input::component::x, "Right trackpad X")
                .add_action("right_trackpad_y", vxr::input::location::right_hand, vxr::input::identifier::track_pad, vxr::input::component::y, "Right trackpad Y")
                .add_action("right_trackpad_click", vxr::input::location::right_hand, vxr::input::identifier::track_pad, vxr::input::component::click, "Right trackpad click")
        )
        .add_swapchain(
            vxr::swapchain_create_info{}
                .name("controller_text")
                .usage()
                    .color_attachment()
                .format(format)
                .extent(width, height)
        );
    return config;
}

void controller_renderer::update_text() {
    const auto profile = [&](vxr::input::location location) {
        const auto found = session_context_.interaction_profiles.find(location);
        return found == session_context_.interaction_profiles.end() ? std::string{"Waiting for controller"} : found->second;
    };
    std::string history = action_history_.empty() ? "Press a controller button\n" : "";
    for (const auto &action : action_history_) {
        history += action + "\n";
    }
    const std::string text = "CONTROLLER INPUT TEST\n\nLeft profile:  " + profile(vxr::input::location::left_hand) + "\nRight profile: " + profile(vxr::input::location::right_hand) + "\n\nLast 5 actions:\n" + history;
    auto &buffer = *text_mapping_.as<text_buffer>();
    buffer = {};
    buffer.character_count = static_cast<std::uint32_t>(std::min(text.size(), std::size(buffer.characters)));
    for (std::uint32_t index = 0; index < buffer.character_count; ++index) {
        buffer.characters[index] = static_cast<unsigned char>(text[index]);
    }
}

void controller_renderer::create_render_targets() {
    const VkAttachmentDescription attachment{0, format, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE, VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                                             VK_ATTACHMENT_STORE_OP_DONT_CARE, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    const VkAttachmentReference color_attachment{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_attachment;
    auto render_pass_info = VkRenderPassCreateInfo{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    render_pass_info.attachmentCount = 1;
    render_pass_info.pAttachments = &attachment;
    render_pass_info.subpassCount = 1;
    render_pass_info.pSubpasses = &subpass;
    VKZ_CHECK_VULKAN(vkCreateRenderPass(device().logical, &render_pass_info, nullptr, &render_pass_));

    for (const auto &swapchain_image : get_swapchain("controller_text").images()) {
        vkz::image image{};
        image.handle = swapchain_image.image;
        image.create_info = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        image.create_info.format = format;
        image_views_.push_back(vkz::image_view::builder(device()).image(image).format(format).aspect_mask(VK_IMAGE_ASPECT_COLOR_BIT).build());
        auto framebuffer_info = VkFramebufferCreateInfo{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        framebuffer_info.renderPass = render_pass_;
        framebuffer_info.attachmentCount = 1;
        framebuffer_info.pAttachments = &image_views_.back().handle;
        framebuffer_info.width = width;
        framebuffer_info.height = height;
        framebuffer_info.layers = 1;
        VkFramebuffer framebuffer{VK_NULL_HANDLE};
        VKZ_CHECK_VULKAN(vkCreateFramebuffer(device().logical, &framebuffer_info, nullptr, &framebuffer));
        framebuffers_.push_back(framebuffer);
    }
}

void controller_renderer::create_pipeline() {
    pipeline_ =
        vkz::make_graphics_pipeline_builder(device())
            .shader_stage()
                .vertex_shader(shader_source("controller.vert"))
                .fragment_shader(shader_source("controller.frag"))
            .vertex_input_state()
            .input_assembly_state()
                .triangles()
            .viewport_state()
                .viewport()
                    .origin(0, 0)
                    .dimension(width, height)
                    .min_depth(0.0F)
                    .max_depth(1.0F)
                .scissor()
                    .offset(0, 0)
                    .extent(static_cast<std::int32_t>(width), static_cast<std::int32_t>(height))
            .rasterization_state()
                .polygon_mode_fill()
                .cull_none()
            .multisample_state()
                .rasterization_samples(VK_SAMPLE_COUNT_1_BIT)
            .color_blend_state()
                .attachment()
                    .add()
            .layout()
                .add_descriptor_set_layout(descriptor_layout_)
            .render_pass(render_pass_)
            .name("controller_text")
            .build();
    pipeline_.descriptor_sets = {descriptor_set_};
}

void controller_renderer::draw(std::uint32_t image_index) {
    const auto command_buffer = command_pool_.create_command_buffer();
    const VkClearValue clear{{{0.02F, 0.03F, 0.05F, 1.0F}}};
    auto begin_info = VkRenderPassBeginInfo{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    begin_info.renderPass = render_pass_;
    begin_info.framebuffer = framebuffers_.at(image_index);
    begin_info.renderArea.extent = {width, height};
    begin_info.clearValueCount = 1;
    begin_info.pClearValues = &clear;
    vkCmdBeginRenderPass(command_buffer, &begin_info, VK_SUBPASS_CONTENTS_INLINE);
    vkz::bind_pipeline(command_buffer, pipeline_);
    vkz::draw(command_buffer, 3);
    vkCmdEndRenderPass(command_buffer);
    VKZ_CHECK_VULKAN(vkEndCommandBuffer(command_buffer));
    command_pool_.submit_and_wait(command_buffer);
    command_pool_.destroy(command_buffer);
}

int main() {
    spdlog::info("{} controller input (Vulkan header version {})", vxr::project_name(), vxr::vulkan_header_version());
    auto context_builder = vxr::context::create();
    context_builder
        .app_name("VulkanXR Controller Input")
        .vulkan_api_version(VK_API_VERSION_1_2)
        .openxr_api_version(XR_API_VERSION_1_0);
    auto session_config = controller_renderer::session_config();
    vxr::application application{context_builder, session_config, controller_renderer::create};
    application.run();
    return EXIT_SUCCESS;
}
