#pragma once

#include <vxr/renderer.hpp>
#include <vxr/session_config.hpp>

#include <vulkanizer/commands.hpp>
#include <vulkanizer/memory.hpp>
#include <vulkanizer/pipeline.hpp>

#include <array>
#include <memory>
#include <vector>

class space_visualization_renderer final : public vxr::renderer {
  public:
    using renderer::renderer;

    void init() override;

    void cleanup() override;

    void begin_frame() override;

    void set(const std::vector<vxr::space_location> &space_locations) override;

    std::vector<vxr::vibration> set(const vxr::action_set &actions) override;

    void paused(const vxr::frame_info &frame, vxr::layers &layers) override;

    void render(const vxr::frame_info &frame, vxr::layers &layers) override;

    [[nodiscard]] const char *name() const noexcept override;

    [[nodiscard]] static std::unique_ptr<vxr::renderer> create(vxr::context &xr_context, vxr::session::context &session_context);

    [[nodiscard]] static vxr::session::config session_config();

  private:
    struct cube_instance {
        vxr::transform transform;
    };

    struct push_constants {
        vxr::mat4 model{1.0F};
        vxr::mat4 view_projection{1.0F};
    };

    void create_cube();

    void create_image_views();

    void create_pipeline();

    void setup_projection_layer();

    void render_view(const vxr::frame_info &frame, std::uint32_t view_index);

    vkz::vma_memory_allocator allocator_{};
    vkz::buffer vertex_buffer_{};
    vkz::buffer index_buffer_{};
    vkz::pipeline pipeline_{};
    vkz::command_pool command_pool_{};
    std::array<std::vector<vkz::image_view>, 2> color_views_;
    std::array<std::vector<vkz::image_view>, 2> depth_views_;
    std::array<vkz::image, 2> owned_depth_images_{};
    std::vector<cube_instance> cubes_;
    std::array<XrCompositionLayerProjectionView, 2> projection_views_{};
    std::array<XrCompositionLayerDepthInfoKHR, 2> depth_infos_{};
    XrCompositionLayerProjection projection_layer_{};
    std::uint32_t index_count_{};
    std::array<float, 2> hand_scale_{1.0F, 1.0F};
};
