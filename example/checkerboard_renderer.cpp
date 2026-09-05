#include "checkerboard_renderer.hpp"

#include <vxr/openxr_helpers.hpp>

#include <vulkanizer/barrier.hpp>
#include <vulkanizer/commands.hpp>
#include <vulkanizer/context.hpp>
#include <vulkanizer/memory.hpp>

#include <glm/common.hpp>
#include <glm/exponential.hpp>
#include <glm/geometric.hpp>

#include <algorithm>
#include <cstdint>
#include <vector>
#include <utility>

namespace {

    struct color {
        std::uint8_t r;
        std::uint8_t g;
        std::uint8_t b;
        std::uint8_t a;
    };

    [[nodiscard]] glm::vec4 colored_checkerboard(const glm::vec2 &uv) {
        const float x = 2.0F * uv.x - 1.0F;
        const float y = 2.0F * uv.y - 1.0F;
        return {glm::step(1.0F, glm::mod(glm::floor((x + 1.0F) / 0.2F) + glm::floor((y + 1.0F) / 0.2F), 2.0F)),
                glm::step(1.0F, glm::mod(glm::floor((x + 1.0F) / 0.3F) + glm::floor((y + 1.0F) / 0.3F), 2.0F)),
                glm::step(1.0F, glm::mod(glm::floor((x + 1.0F) / 0.4F) + glm::floor((y + 1.0F) / 0.4F), 2.0F)), 1.0F};
    }

    [[nodiscard]] glm::vec4 checkerboard(const glm::vec2 &uv) {
        const float x = 2.0F * uv.x - 1.0F;
        const float y = 2.0F * uv.y - 1.0F;
        const float value = glm::step(1.0F, glm::mod(glm::floor((x + 1.0F) / 0.2F) + glm::floor((y + 1.0F) / 0.2F), 2.0F));
        return {value, value, value, 1.0F};
    }

    [[nodiscard]] color to_color(const glm::vec4 &value) {
        return {static_cast<std::uint8_t>(value.r * 255.0F), static_cast<std::uint8_t>(value.g * 255.0F), static_cast<std::uint8_t>(value.b * 255.0F), static_cast<std::uint8_t>(value.a * 255.0F)};
    }

} // namespace

void checkerboard_renderer::init() {
    auto &xr_swapchain = get_swapchain("checkerboard");
    const auto width = xr_swapchain.width();
    const auto height = xr_swapchain.height();
    const auto layer_size = static_cast<std::size_t>(width) * height;
    std::vector<color> colors(layer_size * layers_.size());

    for (std::size_t layer_index = 0; layer_index < layers_.size(); ++layer_index) {
        for (std::uint32_t y = 0; y < height; ++y) {
            for (std::uint32_t x = 0; x < width; ++x) {
                const glm::vec2 uv{static_cast<float>(x) / static_cast<float>(width), static_cast<float>(y) / static_cast<float>(height)};
                colors[layer_index * layer_size + y * width + x] = to_color(layer_index == 0 ? checkerboard(uv) : colored_checkerboard(uv));
            }
        }
    }

    auto allocator = vkz::vma_memory_allocator::create(vulkan_context());
    vkz::staging_buffer staging{allocator, colors.size() * sizeof(color)};
    auto staging_memory = staging.borrow(colors.size() * sizeof(color));
    staging_memory.copy_from(colors.data());

    vkz::buffer source;
    source._ = staging.handle();
    source.create_info.size = staging_memory.size();

    for (const auto &swapchain_image : xr_swapchain.images()) {
        vkz::image image;
        image.handle = swapchain_image.image;
        image.create_info.imageType = VK_IMAGE_TYPE_2D;
        image.create_info.format = xr_swapchain.format();
        image.create_info.extent = {width, height, 1};
        image.create_info.mipLevels = 1;
        image.create_info.arrayLayers = static_cast<std::uint32_t>(layers_.size());
        image.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        vkz::scope_command_buffer commands{device(), graphics_queue_family(), graphics_queue()};
        vkz::copy(commands, source, image);
        const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, static_cast<std::uint32_t>(layers_.size())};
        vkz::barrier::push_and_flush(commands, image, range, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                                     VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    }

    staging.return_memory(staging_memory);
    staging.destroy();
    allocator.destroy();

    for (std::size_t layer_index = 0; layer_index < layers_.size(); ++layer_index) {
        auto &layer = layers_[layer_index];
        layer = vxr::make_struct<XrCompositionLayerQuad>();
        layer.eyeVisibility = layer_index == 0 ? XR_EYE_VISIBILITY_LEFT : XR_EYE_VISIBILITY_RIGHT;
        layer.subImage.swapchain = xr_swapchain.handle();
        layer.subImage.imageRect = {{0, 0}, {static_cast<std::int32_t>(width), static_cast<std::int32_t>(height)}};
        layer.subImage.imageArrayIndex = static_cast<std::uint32_t>(layer_index);
        layer.pose.orientation.w = 1.0F;
        layer.pose.position.z = -1.0F;
        layer.size = {1.0F, 1.0F};
    }
}

void checkerboard_renderer::paused(const vxr::frame_info &frame, vxr::layers &layers) {
    render(frame, layers);
}

void checkerboard_renderer::render(const vxr::frame_info &, vxr::layers &layers) {
    layers.push_back({&layers_[0], 0});
    layers.push_back({&layers_[1], 1});
}

const char *checkerboard_renderer::name() const noexcept {
    return "Checkerboard";
}

vxr::session::config checkerboard_renderer::session_config() {
    vxr::session::config config;
    config.add_swapchain(
        vxr::swapchain_create_info{}
            .name("checkerboard")
            .usage()
                .color_attachment()
                .transfer_destination()
            .format(VK_FORMAT_R8G8B8A8_SRGB)
            .extent(2048, 2048)
            .array_size(2)
    );
    return config;
}

std::unique_ptr<vxr::renderer> checkerboard_renderer::create(vxr::context &xr_context, vxr::session::context &session_context) {
    return std::make_unique<checkerboard_renderer>(xr_context, session_context);
}
