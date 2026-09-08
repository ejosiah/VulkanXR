#pragma once

#include <vxr/rendering_context.hpp>
#include <vxr/models.hpp>
#include <vxr/session_context.hpp>

#include <vulkanizer/commands.hpp>
#include <vulkanizer/descriptors.hpp>
#include <vulkanizer/memory.hpp>
#include <vulkanizer/pipeline.hpp>
#include <vulkanizer/vulkan_app.hpp>

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace vxr {

    struct mirror_config {
        std::uint32_t width{1280};
        std::uint32_t height{720};
        std::string title{"VulkanXR Mirror"};
        bool enabled{true};
        bool multiview{};
        bool vsync{false};
        float eye_separation{0.08F};
        float barrel_distortion{0.2F};
    };

    class mirror {
      public:
        mirror(GLFWwindow *window, rendering_context &rendering_context, session::context &session_context, mirror_config config);

        ~mirror();

        void render(const layers &composition_layers, const std::map<XrSwapchain, std::uint32_t> &image_indices);

        [[nodiscard]] bool should_close() const;

        void poll_events() const;

      private:
        struct push_constants {
            mat4 transform{1.0F};
            vec4 parameters{};
        };

        void init();

        void cleanup() noexcept;

        void create_pipelines();

        [[nodiscard]] vkz::pipeline create_pipeline(const std::string &vertex_shader, const std::string &fragment_shader, const std::string &name);

        void draw_layer(VkCommandBuffer command_buffer, const layer &composition_layer, const std::map<XrSwapchain, std::uint32_t> &image_indices,
                        std::vector<vkz::image_view> &temporary_views);

        void draw_projection(VkCommandBuffer command_buffer, const XrCompositionLayerProjection &layer, const std::map<XrSwapchain, std::uint32_t> &image_indices,
                             std::vector<vkz::image_view> &temporary_views);

        void draw_cube(VkCommandBuffer command_buffer, const XrCompositionLayerCubeKHR &layer, const std::map<XrSwapchain, std::uint32_t> &image_indices,
                       std::vector<vkz::image_view> &temporary_views);

        void draw_quad(VkCommandBuffer command_buffer, const XrCompositionLayerQuad &layer, const std::map<XrSwapchain, std::uint32_t> &image_indices,
                       std::vector<vkz::image_view> &temporary_views);

        void draw_equirect(VkCommandBuffer command_buffer, const XrCompositionLayerEquirectKHR &layer, const std::map<XrSwapchain, std::uint32_t> &image_indices,
                           std::vector<vkz::image_view> &temporary_views);

        void draw_texture(VkCommandBuffer command_buffer, vkz::pipeline &pipeline, XrSwapchain handle, std::uint32_t image_index, VkImageViewType view_type, std::uint32_t base_layer,
                          std::uint32_t layer_count, const push_constants &constants, std::vector<vkz::image_view> &temporary_views);

        [[nodiscard]] std::uint32_t image_index(XrSwapchain handle, const std::map<XrSwapchain, std::uint32_t> &image_indices) const;

        rendering_context &rendering_context_;
        session::context &session_context_;
        mirror_config config_;
        vkz::vulkan_app app_;
        std::unique_ptr<vkz::swapchain> swapchain_;
        std::vector<vkz::image_view> swapchain_views_;
        vkz::command_pool command_pool_{};
        vkz::descriptor_set_layout descriptor_layout_{};
        vkz::descriptor_pool descriptor_pool_{};
        vkz::sampler sampler_{};
        vkz::pipeline projection_pipeline_{};
        vkz::pipeline cube_pipeline_{};
        vkz::pipeline quad_pipeline_{};
        vkz::pipeline equirect_pipeline_{};
        VkSemaphore image_available_{VK_NULL_HANDLE};
        VkSemaphore render_finished_{VK_NULL_HANDLE};
        bool initialized_{};
    };

} // namespace vxr
