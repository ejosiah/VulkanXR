#pragma once

#include <vxr/renderer.hpp>
#include <vxr/session_config.hpp>

#include <vulkanizer/commands.hpp>
#include <vulkanizer/descriptors.hpp>
#include <vulkanizer/memory.hpp>
#include <vulkanizer/pipeline.hpp>

#include <array>
#include <deque>
#include <memory>
#include <string>
#include <vector>

class controller_renderer final : public vxr::renderer {
  public:
    using renderer::renderer;

    void init() override;

    void cleanup() override;

    std::vector<vxr::vibration> set(const vxr::action_set &actions) override;

    void paused(const vxr::frame_info &frame, vxr::layers &layers) override;

    void render(const vxr::frame_info &frame, vxr::layers &layers) override;

    [[nodiscard]] const char *name() const noexcept override;

    [[nodiscard]] static std::unique_ptr<vxr::renderer> create(vxr::context &xr_context, vxr::session::context &session_context);

    [[nodiscard]] static vxr::session::config session_config();

  private:
    struct text_buffer {
        std::uint32_t character_count{};
        std::uint32_t characters[2048]{};
    };

    void update_text();

    void create_render_targets();

    void create_pipeline();

    void draw(std::uint32_t image_index);

    vkz::vma_memory_allocator allocator_{};
    vkz::buffer text_buffer_{};
    vkz::mapping text_mapping_{};
    vkz::descriptor_pool descriptor_pool_{};
    vkz::descriptor_set_layout descriptor_layout_{};
    vkz::descriptor_set descriptor_set_{};
    vkz::pipeline pipeline_{};
    vkz::command_pool command_pool_{};
    std::vector<vkz::image_view> image_views_;
    std::vector<VkFramebuffer> framebuffers_;
    VkRenderPass render_pass_{VK_NULL_HANDLE};
    XrCompositionLayerQuad layer_{};
    std::array<glm::vec2, 2> trackpad_positions_{};
    std::array<glm::vec2, 2> thumbstick_positions_{};
    std::deque<std::string> action_history_;
};
