#include "controller_renderer.hpp"

#include <vxr/vxr.hpp>
#include <vxr/openxr_helpers.hpp>

#include <vulkanizer/builders.hpp>
#include <vulkanizer/barrier.hpp>
#include <vulkanizer/render.hpp>
#include <vulkanizer/status.hpp>

#include <cmrc/cmrc.hpp>
#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string_view>

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
        if (std::holds_alternative<vxr::vec2>(action.state)) {
            return "XR_ACTION_TYPE_VECTOR2F_INPUT";
        }
        return "XR_ACTION_TYPE_POSE_INPUT";
    }

    [[nodiscard]] std::string trackpad_region(const vxr::vec2 &position) {
        constexpr float center_radius = 0.35F;
        if (vxr::length(position) < center_radius) {
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
    create_image_views();
    font_pipeline_ = vxr::font_pipeline::create({device(), descriptor_layout_, format, {width, height}, shader_source("font.vert"), shader_source("font.frag"), "controller_font"});

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
    font_pipeline_.destroy();
    for (auto &view : image_views_) {
        view.destroy();
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
        } else if (name.ends_with("thumbstick_x")) {
            thumbstick_positions_[hand].x = action.value<float>();
        } else if (name.ends_with("thumbstick_y")) {
            thumbstick_positions_[hand].y = action.value<float>();
        }
        if (action.changed && name.find("thumbstick") != std::string::npos) {
            action_history_.push_front(fmt::format("{}/input/thumbstick/{} -> XR_ACTION_TYPE_FLOAT_INPUT ({:.2f})", hand == 0 ? "/user/hand/left" : "/user/hand/right",
                                                   name.ends_with("thumbstick_x") ? "x" : "y", action.value<float>()));
            if (action_history_.size() > 5) {
                action_history_.pop_back();
            }
        }
    }

    for (const auto &[name, action] : actions) {
        if (!action.active || !action.changed || !std::holds_alternative<bool>(action.state) || !action.value<bool>()) continue;
        const bool left = name.starts_with("left_");
        std::string control;
        if (name.find("trackpad") != std::string::npos) {
            control = "trackpad/" + trackpad_region(trackpad_positions_[left ? 0 : 1]) + "/click";
        } else if (name.find("thumbstick") != std::string::npos) {
            control = "thumbstick/click";
        } else if (name.find("squeeze") != std::string::npos) {
            control = "squeeze/click";
        } else if (name.find("trigger") != std::string::npos) {
            control = "trigger/click";
        } else if (name.find("_a_click") != std::string::npos) {
            control = "a/click";
        } else if (name.find("_b_click") != std::string::npos) {
            control = "b/click";
        } else if (name.find("_x_click") != std::string::npos) {
            control = "x/click";
        } else if (name.find("_y_click") != std::string::npos) {
            control = "y/click";
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

std::unique_ptr<vxr::renderer> controller_renderer::create(vxr::rendering::context &context) {
    return std::make_unique<controller_renderer>(context);
}

vxr::session::config controller_renderer::session_config() {
    vxr::session::config config;
    config
        .view()
        .add_action_set(
            vxr::session::action_set_specification{}
                .name("controller")
                .description("Controller input test")
                .add_action("left_trigger_click", vxr::input::htc_vive::layout::left_trigger_click, "Left trigger")
                .add_action("right_trigger_click", vxr::input::htc_vive::layout::right_trigger_click, "Right trigger")
                .add_action("left_menu_click", vxr::input::htc_vive::layout::left_menu_click, "Left menu")
                .add_action("right_menu_click", vxr::input::htc_vive::layout::right_menu_click, "Right menu")
                .add_action("left_squeeze_click", vxr::input::htc_vive::layout::left_squeeze_click, "Left grip")
                .add_action("right_squeeze_click", vxr::input::htc_vive::layout::right_squeeze_click, "Right grip")
                .add_action("left_trackpad_x", vxr::input::htc_vive::layout::left_trackpad_x, "Left trackpad X")
                .add_action("left_trackpad_y", vxr::input::htc_vive::layout::left_trackpad_y, "Left trackpad Y")
                .add_action("left_trackpad_click", vxr::input::htc_vive::layout::left_trackpad_click, "Left trackpad click")
                .add_action("right_trackpad_x", vxr::input::htc_vive::layout::right_trackpad_x, "Right trackpad X")
                .add_action("right_trackpad_y", vxr::input::htc_vive::layout::right_trackpad_y, "Right trackpad Y")
                .add_action("right_trackpad_click", vxr::input::htc_vive::layout::right_trackpad_click, "Right trackpad click")
                .add_action("left_x_click", vxr::input::oculus_touch::layout::left_x_click, "Left X")
                .add_action("left_y_click", vxr::input::oculus_touch::layout::left_y_click, "Left Y")
                .add_action("right_a_click", vxr::input::oculus_touch::layout::right_a_click, "Right A")
                .add_action("right_b_click", vxr::input::oculus_touch::layout::right_b_click, "Right B")
                .add_action("left_thumbstick_x", vxr::input::oculus_touch::layout::left_thumbstick_x, "Left thumbstick X")
                .add_action("left_thumbstick_y", vxr::input::oculus_touch::layout::left_thumbstick_y, "Left thumbstick Y")
                .add_action("left_thumbstick_click", vxr::input::oculus_touch::layout::left_thumbstick_click, "Left thumbstick click")
                .add_action("right_thumbstick_x", vxr::input::oculus_touch::layout::right_thumbstick_x, "Right thumbstick X")
                .add_action("right_thumbstick_y", vxr::input::oculus_touch::layout::right_thumbstick_y, "Right thumbstick Y")
                .add_action("right_thumbstick_click", vxr::input::oculus_touch::layout::right_thumbstick_click, "Right thumbstick click")
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
        const auto found = context_.interaction_profiles.find(location);
        return found == context_.interaction_profiles.end() ? std::string{"Waiting for controller"} : found->second;
    };
    std::string history = action_history_.empty() ? "Press a controller button\n" : "";
    for (const auto &action : action_history_) {
        history += action + "\n";
    }
    const std::string prefix =
        "CONTROLLER INPUT TEST\n\nLeft profile:  " + profile(vxr::input::location::left_hand) + "\nRight profile: " + profile(vxr::input::location::right_hand) + "\n\nLast 5 actions:\n";
    const std::string text = prefix + history;
    auto &buffer = *text_mapping_.as<text_buffer>();
    buffer = {};
    buffer.character_count = static_cast<std::uint32_t>(std::min(text.size(), std::size(buffer.characters)));
    for (std::uint32_t index = 0; index < buffer.character_count; ++index) {
        buffer.characters[index] = static_cast<unsigned char>(text[index]);
    }
    if (!action_history_.empty()) {
        const auto highlight_end = std::min(prefix.size() + action_history_.front().size(), static_cast<std::size_t>(buffer.character_count));
        for (std::size_t index = prefix.size(); index < highlight_end; ++index) {
            buffer.characters[index] |= 0x100u;
        }
    }
}

void controller_renderer::create_image_views() {
    for (const auto &swapchain_image : get_swapchain("controller_text").images()) {
        vkz::image image{};
        image.handle = swapchain_image.image;
        image.create_info = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        image.create_info.format = format;
        image_views_.push_back(vkz::image_view::builder(device()).image(image).format(format).aspect_mask(VK_IMAGE_ASPECT_COLOR_BIT).build());
    }
}

void controller_renderer::draw(std::uint32_t image_index) {
    const auto command_buffer = command_pool_.create_command_buffer();
    auto image = get_swapchain("controller_text").images()[image_index].image;
    const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkz::barrier::push_and_flush(command_buffer, image, range, VK_PIPELINE_STAGE_2_NONE, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_NONE,
                                 VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    const vkz::render_info render_info{{{image_views_.at(image_index), format, {0.02F, 0.03F, 0.05F, 1.0F}}}, {}, {}, {width, height}};
    vkz::render(command_buffer, render_info, [&] {
        font_pipeline_.bind(command_buffer, descriptor_set_, {{0.02F, 0.03F, 0.05F, 1.0F}});
        vkz::draw(command_buffer, 3);
    });
    VKZ_CHECK_VULKAN(vkEndCommandBuffer(command_buffer));
    command_pool_.submit_and_wait(command_buffer);
    command_pool_.destroy(command_buffer);
}

int main(int argc, char **argv) {
    spdlog::info("{} controller input (Vulkan header version {})", vxr::project_name(), vxr::vulkan_header_version());

    auto application  =
        vxr::application_builder{}
            .app_name("VulkanXR Controller Input")
            .vulkan_api_version(VK_API_VERSION_1_2)
            .openxr_api_version(XR_API_VERSION_1_0)
            .session(controller_renderer::session_config())
            .renderer(controller_renderer::create)
            .mode(argc > 1 && std::string_view{argv[1]} == "--desktop" ? vxr::application_mode::desktop : vxr::application_mode::vr)
        .build();

    application->run();

    return EXIT_SUCCESS;
}
