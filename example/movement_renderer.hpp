#pragma once

#include <vxr/renderer.hpp>
#include <vxr/session_config.hpp>

#include <vulkanizer/commands.hpp>
#include <vulkanizer/descriptors.hpp>
#include <vulkanizer/memory.hpp>
#include <vulkanizer/pipeline.hpp>
#include <vulkanizer/texture.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

class movement_renderer final : public vxr::renderer {
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
    struct push_constants {
        glm::mat4 inverse_view_projection{1.0F};
        glm::vec4 origin{};
        glm::mat4 view_projection{1.0F};
        glm::vec4 teleport_origin{};
        glm::vec4 teleport_velocity{};
        glm::vec4 teleport_target{};
        std::uint32_t teleport_active{};
    };

    struct teleport_state {
        bool active{};
        bool valid{};
        glm::vec3 origin{};
        glm::vec3 velocity{};
        glm::vec3 target{};
    };

    void load_textures();

    void cache_cube_layer();

    void create_projection_resources();

    void create_projection_views();

    void create_projection_pipeline();

    void setup_projection_layer();

    void render_projection_view(const vxr::frame_info &frame, std::uint32_t view_index);

    void update_teleport(const vxr::action_set &actions);

    void update_camera_movement(const vxr::action_set &actions);

    [[nodiscard]] static bool intersect_ground(const glm::vec3 &origin, const glm::vec3 &velocity, glm::vec3 &intersection);

    vkz::vma_memory_allocator allocator_{};
    vkz::command_pool command_pool_{};
    vkz::texture diffuse_{};
    vkz::texture irradiance_{};
    vkz::texture specular_{};
    vkz::texture grid_{};
    vkz::descriptor_pool descriptor_pool_{};
    vkz::descriptor_set_layout descriptor_layout_{};
    vkz::descriptor_set descriptor_set_{};
    vkz::pipeline skybox_pipeline_{};
    vkz::pipeline floor_pipeline_{};
    std::array<std::vector<vkz::image_view>, 2> projection_views_;
    std::array<XrCompositionLayerProjectionView, 2> projection_layer_views_{};
    XrCompositionLayerProjection projection_layer_{};
    XrCompositionLayerCubeKHR cube_layer_{};
    teleport_state teleport_{};
    glm::vec3 world_offset_{};
    glm::vec3 head_position_{};
    glm::quat head_orientation_{1.0F, 0.0F, 0.0F, 0.0F};
    float frame_seconds_{1.0F / 90.0F};
};
