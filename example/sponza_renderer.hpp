#pragma once

#include <vxr/camera.hpp>
#include "sponza_lighting.hpp"
#include "rtx_tv.hpp"
#include <functional>
#include <vxr/renderer.hpp>
#include <vxr/session_config.hpp>

#include <vulkanizer/commands.hpp>
#include <vulkanizer/memory.hpp>
#include <vulkanizer/mesh.hpp>

#include <array>
#include <memory>
#include <vector>

class sponza_renderer final : public vxr::renderer {
  public:
    sponza_renderer(vxr::rendering::context &context, const sponza_lighting_options &options);

    ~sponza_renderer() override;

    uint32_t frame_limit{};
    std::function<void()> request_shutdown;

    void init() override;

    void cleanup() override;

    std::vector<vxr::vibration> set(const vxr::action_set &actions) override;

    void reference_space_changed(const XrEventDataReferenceSpaceChangePending &event) override;

    void paused(const vxr::frame_info &frame, vxr::layers &layers) override;

    void render(const vxr::frame_info &frame, vxr::layers &layers) override;

    [[nodiscard]] const char *name() const noexcept override;

    [[nodiscard]] static std::unique_ptr<vxr::renderer> create(vxr::rendering::context &context, const sponza_lighting_options &options);

    [[nodiscard]] static vxr::session::config session_config();

  private:
    void create_projection_views();

    void setup_projection_layer();

    void render_view(const vxr::frame_info &frame, std::uint32_t view_index);

    void update_camera_movement(const vxr::action_set &actions);

    void update_camera_turn(const vxr::action_set &actions);

    [[nodiscard]] bool vive_controller_active() const;

    vkz::vma_memory_allocator allocator_{};
    vkz::command_pool command_pool_{};
    vkz::mesh::data scene_{};
    vkz::buffer vertices_{};
    vkz::buffer indices_{};
    std::unique_ptr<rtx::tv_light> tv_light_;
    std::array<std::unique_ptr<sponza_lighting>, 2> scene_renderers_;
    std::array<std::vector<vkz::image_view>, 2> projection_views_;
    std::array<XrCompositionLayerProjectionView, 2> projection_layer_views_{};
    XrCompositionLayerProjection projection_layer_{};
    vxr::camera camera_{};
    float frame_seconds_{1.0F / 90.0F};
    sponza_lighting_options lighting_options_;
    uint32_t frame_count_{};
};
