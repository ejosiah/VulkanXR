#pragma once

#include <vxr/camera.hpp>
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

    ~movement_renderer() override;

    void init() override;

    void cleanup() override;

    std::vector<vxr::vibration> set(const vxr::action_set &actions) override;

    void reference_space_changed(const XrEventDataReferenceSpaceChangePending &event) override;

    void paused(const vxr::frame_info &frame, vxr::layers &layers) override;

    void render(const vxr::frame_info &frame, vxr::layers &layers) override;

    [[nodiscard]] const char *name() const noexcept override;

    [[nodiscard]] static std::unique_ptr<vxr::renderer> create(vxr::rendering::context &context);

    [[nodiscard]] static vxr::session::config session_config();

  private:
    struct push_constants {
        vxr::mat4 inverse_view_projection{1.0F};
        vxr::vec4 origin{};
        vxr::mat4 view_projection{1.0F};
        vxr::vec4 teleport_origin{};
        vxr::vec4 teleport_velocity{};
        vxr::vec4 teleport_target{};
        std::uint32_t teleport_active{};
    };

    struct teleport_state {
        bool active{};
        bool valid{};
        vxr::vec3 origin{};
        vxr::vec3 velocity{};
        vxr::vec3 target{};
    };

    struct object_push_constants {
        vxr::mat4 model{1.0F};
        vxr::mat4 view_projection{1.0F};
    };

    void load_textures();

    void create_projection_resources();

    void create_projection_views();

    void create_projection_pipeline();

    void create_cube();

    void create_cube_pipeline();

    void setup_projection_layer();

    void render_projection_view(const vxr::frame_info &frame, std::uint32_t view_index);

    void prepare_render(VkCommandBuffer command_buffer, VkImage image, uint32_t view_index);

    void update_teleport(const vxr::action_set &actions);

    void update_camera_movement(const vxr::action_set &actions);

    void update_camera_turn(const vxr::action_set &actions);

    [[nodiscard]] bool vive_controller_active() const;

    [[nodiscard]] static bool intersect_ground(const vxr::vec3 &origin, const vxr::vec3 &velocity, vxr::vec3 &intersection);

    vkz::vma_memory_allocator allocator_{};
    vkz::command_pool command_pool_{};
    vkz::texture env_diffuse_{};
    vkz::texture env_irradiance_{};
    vkz::texture env_specular_{};
    vkz::texture grid_{};
    vkz::descriptor_pool descriptor_pool_{};
    vkz::descriptor_set_layout descriptor_layout_{};
    vkz::descriptor_set descriptor_set_{};
    vkz::pipeline skybox_pipeline_{};
    vkz::pipeline floor_pipeline_{};
    vkz::pipeline cube_pipeline_{};
    vkz::buffer cube_vertex_buffer_{};
    vkz::buffer cube_index_buffer_{};
    std::uint32_t cube_index_count_{};
    std::array<std::vector<vkz::image_view>, 2> projection_views_;
    std::array<XrCompositionLayerProjectionView, 2> projection_layer_views_{};
    XrCompositionLayerProjection projection_layer_{};
    teleport_state teleport_{};
    vxr::camera camera_{};
    float frame_seconds_{1.0F / 90.0F};
};
