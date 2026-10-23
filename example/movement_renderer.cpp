#include "movement_renderer.hpp"

#include <vxr/openxr_helpers.hpp>
#include <vxr/status.hpp>
#include <vxr/vxr.hpp>

#include <vulkanizer/barrier.hpp>
#include <vulkanizer/builders.hpp>
#include <vulkanizer/ktx.hpp>
#include <vulkanizer/primitives.hpp>
#include <vulkanizer/render.hpp>
#include <vulkanizer/status.hpp>

#include <cmrc/cmrc.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>

CMRC_DECLARE(vxr_shaders);

namespace {

    constexpr VkFormat projection_format = VK_FORMAT_R8G8B8A8_SRGB;
    constexpr std::uint32_t projection_width = 2064;
    constexpr std::uint32_t projection_height = 2096;

    [[nodiscard]] std::filesystem::path resource_path(const std::string &name) {
        return std::filesystem::path{VULKANXR_RESOURCE_DIR} / name;
    }

    [[nodiscard]] std::string shader_source(const std::string &path) {
        const auto filesystem = cmrc::vxr_shaders::get_filesystem();
        const auto shader = filesystem.open(path);
        return {shader.begin(), shader.end()};
    }

    [[nodiscard]] std::string grid_shader_source() {
        auto source = shader_source("grid.frag");
        constexpr std::string_view include = "#include \"hash.glsl\"";
        const auto position = source.find(include);
        if (position == std::string::npos) {
            throw vxr::fatal_error{"grid.frag does not include hash.glsl"};
        }
        source.replace(position, include.size(), shader_source("hash.glsl"));
        return source;
    }

} // namespace

void movement_renderer::init() {
    allocator_ = vkz::vma_memory_allocator::create(vulkan_context());
    command_pool_.init(device(), graphics_queue_family(), vkz::command_pool_flags_reset_command_buffer, graphics_queue());
    load_textures();
    create_projection_resources();
    create_cube();
    create_cube_pipeline();
}

void movement_renderer::cleanup() {
    if (!device()) {
        return;
    }
    vkDeviceWaitIdle(device().logical);
    command_pool_.deinit();
    cube_pipeline_.destroy();
    cube_index_buffer_.destroy();
    cube_vertex_buffer_.destroy();
    floor_pipeline_.destroy();
    skybox_pipeline_.destroy();
    descriptor_pool_.destroy();
    descriptor_layout_.destroy();
    for (auto &views : projection_views_) {
        for (auto &view : views) {
            view.destroy();
        }
        views.clear();
    }
    grid_.destroy();
    env_specular_.destroy();
    env_irradiance_.destroy();
    env_diffuse_.destroy();
    allocator_.destroy();
}

std::vector<vxr::vibration> movement_renderer::set(const vxr::action_set &actions) {
    update_teleport(actions);
    update_camera_turn(actions);
    update_camera_movement(actions);
    return {};
}

void movement_renderer::reference_space_changed(const XrEventDataReferenceSpaceChangePending &event) {
    camera_.reference_space_changed(event, context_.views);
}

void movement_renderer::update_teleport(const vxr::action_set &actions) {
    constexpr float activation_threshold = 0.7F;
    const bool vive_active = vive_controller_active();
    const auto &right_y = actions.get(vive_active ? "vive_right_teleport_y" : "right_teleport_y");
    const float right_value = right_y.active ? right_y.value<float>() : 0.0F;
    const auto &right_click = actions.get("vive_right_teleport_click");
    const bool vive_trackpad_pressed = right_click.active && right_click.value<bool>();
    const bool right_active = right_value > activation_threshold && (!vive_active || vive_trackpad_pressed);

    if (!right_active) {
        if (teleport_.active && teleport_.valid) {
            camera_.teleport(teleport_.target);
        }
        teleport_ = {};
        return;
    }

    const auto &aim = actions.get(vive_active ? "vive_right_aim_pose" : "right_aim_pose");
    teleport_.active = true;
    teleport_.valid = false;
    if (!aim.active) {
        return;
    }

    const auto controller = camera_.world_pose(aim.value<vxr::pose>());
    teleport_.origin = controller.position;
    teleport_.velocity = controller.orientation * vxr::vec3{0.0F, 0.0F, -6.5F};
    teleport_.valid = intersect_ground(teleport_.origin, teleport_.velocity, teleport_.target);
}

void movement_renderer::update_camera_movement(const vxr::action_set &actions) {
    const bool vive_active = vive_controller_active();
    const auto &move_x = actions.get(vive_active ? "vive_left_move_x" : "left_move_x");
    const auto &move_y = actions.get(vive_active ? "vive_left_move_y" : "left_move_y");
    const auto &move_click = actions.get("vive_left_move_click");
    const bool vive_trackpad_pressed = move_click.active && move_click.value<bool>();
    const bool movement_active = !vive_active || vive_trackpad_pressed;
    const float x = movement_active && move_x.active ? move_x.value<float>() : 0.0F;
    const float y = movement_active && move_y.active ? move_y.value<float>() : 0.0F;
    constexpr float movement_speed = 2.0F;
    camera_.move({x, y}, movement_speed * frame_seconds_);
}

void movement_renderer::update_camera_turn(const vxr::action_set &actions) {
    if (teleport_.active) {
        return;
    }

    const bool vive_active = vive_controller_active();
    const auto &turn_x = actions.get(vive_active ? "vive_right_turn_x" : "right_turn_x");
    const auto &turn_click = actions.get("vive_right_teleport_click");
    const bool vive_trackpad_pressed = turn_click.active && turn_click.value<bool>();
    const bool turning_active = !vive_active || vive_trackpad_pressed;
    const float x = turning_active && turn_x.active ? turn_x.value<float>() : 0.0F;
    if (!camera_.valid() || std::abs(x) < 0.15F) {
        return;
    }

    constexpr float turn_speed = 1.5F;
    camera_.turn(-x * turn_speed * frame_seconds_);
}

bool movement_renderer::vive_controller_active() const {
    constexpr std::string_view vive_profile = "/interaction_profiles/htc/vive_controller";
    return std::ranges::any_of(context_.interaction_profiles, [](const auto &entry) {
        return entry.second == vive_profile;
    });
}

bool movement_renderer::intersect_ground(const vxr::vec3 &origin, const vxr::vec3 &velocity, vxr::vec3 &intersection) {
    constexpr float half_gravity = -4.905F;
    const float discriminant = velocity.y * velocity.y - 4.0F * half_gravity * origin.y;
    if (discriminant < 0.0F) {
        return false;
    }

    const float root = std::sqrt(discriminant);
    const float first = (-velocity.y + root) / (2.0F * half_gravity);
    const float second = (-velocity.y - root) / (2.0F * half_gravity);
    const float time = first > 0.0F ? first : second;
    if (time <= 0.0F || time > 3.0F) {
        return false;
    }

    intersection = origin + velocity * time + vxr::vec3{0.0F, half_gravity * time * time, 0.0F};
    intersection.y = 0.0F;
    return true;
}

void movement_renderer::paused(const vxr::frame_info &frame, vxr::layers &layers) {
    render(frame, layers);
}

void movement_renderer::render(const vxr::frame_info &frame, vxr::layers &layers) {
    if (frame.image.swapchain != get_swapchain("movement_projection").handle()) {
        return;
    }

    frame_seconds_ = std::clamp(static_cast<float>(frame.predicted_duration) * 1.0e-9F, 0.0F, 0.1F);
    const auto view_count = std::min(frame.view.views.size(), projection_layer_views_.size());
    if (view_count > 0) {
        camera_.update(frame.view.views);
    }
    for (std::uint32_t view_index = 0; view_index < view_count; ++view_index) {
        render_projection_view(frame, view_index);
        projection_layer_views_[view_index].pose = frame.view.views[view_index].pose;
        projection_layer_views_[view_index].fov = frame.view.views[view_index].fov;
    }
    projection_layer_.viewCount = static_cast<std::uint32_t>(view_count);
    layers.push_back({&projection_layer_, 0});
}

const char *movement_renderer::name() const noexcept {
    return "Movement";
}

std::unique_ptr<vxr::renderer> movement_renderer::create(vxr::rendering::context &context) {
    return std::make_unique<movement_renderer>(context);
}

vxr::session::config movement_renderer::session_config() {
    vxr::session::config config;
    config
        .stage()
        .add_action_set(
            vxr::session::action_set_specification{}
                .name("teleportation")
                .description("Movement and teleportation controls")
                .add_action("vive_right_aim_pose", vxr::input::htc_vive::layout::right_aim_pose, "Vive right controller aim")
                .add_action("vive_right_turn_x", vxr::input::htc_vive::layout::right_trackpad_x, "Vive turn left and right")
                .add_action("vive_right_teleport_y", vxr::input::htc_vive::layout::right_trackpad_y, "Vive right teleport")
                .add_action("vive_right_teleport_click", vxr::input::htc_vive::layout::right_trackpad_click, "Vive right teleport activation")
                .add_action("vive_left_move_x", vxr::input::htc_vive::layout::left_trackpad_x, "Vive strafe left and right")
                .add_action("vive_left_move_y", vxr::input::htc_vive::layout::left_trackpad_y, "Vive move forward and backward")
                .add_action("vive_left_move_click", vxr::input::htc_vive::layout::left_trackpad_click, "Vive left movement activation")
                .add_action("right_aim_pose", vxr::input::oculus_touch::layout::right_aim_pose, "Right controller aim")
                .add_action("right_turn_x", vxr::input::oculus_touch::layout::right_thumbstick_x, "Turn left and right")
                .add_action("right_teleport_y", vxr::input::oculus_touch::layout::right_thumbstick_y, "Right teleport")
                .add_action("left_move_x", vxr::input::oculus_touch::layout::left_thumbstick_x, "Strafe left and right")
                .add_action("left_move_y", vxr::input::oculus_touch::layout::left_thumbstick_y, "Move forward and backward")
        )
        .add_swapchain(
            vxr::swapchain_create_info{}
                .name("movement_projection")
                .usage()
                    .color_attachment()
                .format(projection_format)
                .extent(projection_width, projection_height)
                .array_size(2)
        );
    return config;
}

void movement_renderer::load_textures() {
    env_diffuse_ = vkz::texture_from_ktx(resource_path("skybox_diffuse.ktx"), allocator_, graphics_queue(), graphics_queue_family(), VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
    env_irradiance_ = vkz::texture_from_ktx(resource_path("skybox_irradiance.ktx"), allocator_, graphics_queue(), graphics_queue_family(), VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
    env_specular_ = vkz::texture_from_ktx(resource_path("skybox_specular.ktx"), allocator_, graphics_queue(), graphics_queue_family(), VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
    grid_ = vkz::texture_from_ktx(resource_path("grid.ktx"), allocator_, graphics_queue(), graphics_queue_family(), VK_SAMPLER_ADDRESS_MODE_REPEAT);
}

void movement_renderer::create_projection_resources() {
    create_projection_views();
    descriptor_layout_ =
        vkz::make_descriptor_set_layout_builder(device())
            .name("movement")
            .binding(0)
                .descriptor_type(vkz::descriptor_type_combined_image_sampler)
                .descriptor_count(1)
                .shader_stages(vkz::shader_stage_fragment)
            .binding(1)
                .descriptor_type(vkz::descriptor_type_combined_image_sampler)
                .descriptor_count(1)
                .shader_stages(vkz::shader_stage_fragment)
            .binding(2)
                .descriptor_type(vkz::descriptor_type_combined_image_sampler)
                .descriptor_count(1)
                .shader_stages(vkz::shader_stage_fragment)
            .binding(3)
                .descriptor_type(vkz::descriptor_type_combined_image_sampler)
                .descriptor_count(1)
                .shader_stages(vkz::shader_stage_fragment)
            .create_layout();

    descriptor_pool_ = vkz::descriptor_pool{device(), 1, {{static_cast<VkDescriptorType>(vkz::descriptor_type_combined_image_sampler), 4}}};
    descriptor_set_ = descriptor_pool_.allocate(descriptor_layout_);
    vkz::update_descriptor(device(), {.descriptor_set = descriptor_set_,
                                      .bindings = {vkz::make_descriptor<vkz::texture_descriptor>(env_diffuse_, 0), vkz::make_descriptor<vkz::texture_descriptor>(grid_, 1),
                                                   vkz::make_descriptor<vkz::texture_descriptor>(env_irradiance_, 2), vkz::make_descriptor<vkz::texture_descriptor>(env_specular_, 3)}});
    create_projection_pipeline();
    setup_projection_layer();
}

void movement_renderer::create_projection_views() {
    auto &swapchain = get_swapchain("movement_projection");
    for (std::uint32_t view_index = 0; view_index < projection_views_.size(); ++view_index) {
        for (const auto &swapchain_image : swapchain.images()) {
            vkz::image image{};
            image.handle = swapchain_image.image;
            image.create_info = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
            image.create_info.format = projection_format;
            projection_views_[view_index].push_back(
                vkz::image_view::builder(device())
                    .image(image)
                    .view_type(VK_IMAGE_VIEW_TYPE_2D)
                    .format(projection_format)
                    .aspect_mask(vkz::image_aspect_color)
                    .base_array_layer(view_index)
                    .layer_count(1)
                    .build()
            );
        }
    }
}

void movement_renderer::create_projection_pipeline() {
    skybox_pipeline_ =
        vkz::make_graphics_pipeline_builder(device())
            .shader_stage()
                .vertex_shader(shader_source("movement_skybox.vert"))
                .fragment_shader(shader_source("movement_skybox.frag"))
            .vertex_input_state()
            .input_assembly_state()
                .triangles()
            .viewport_state()
                .viewport()
                    .origin(0, 0)
                    .dimension(projection_width, projection_height)
                    .min_depth(0.0F)
                    .max_depth(1.0F)
                .scissor()
                    .offset(0, 0)
                    .extent(projection_width, projection_height)
            .rasterization_state()
                .polygon_mode_fill()
                .cull_none()
            .multisample_state()
                .rasterization_samples(vkz::sample_count_x1)
            .color_blend_state()
                .attachment()
                    .add()
            .layout()
                .add_descriptor_set_layout(descriptor_layout_)
                .add_push_constant_range(vkz::shader_stage_fragment, 0, sizeof(push_constants))
            .dynamic_render_pass()
                .add_color_attachment(projection_format)
            .name("movement_skybox")
            .build();
    skybox_pipeline_.descriptor_sets = {descriptor_set_};

    floor_pipeline_ =
        vkz::make_graphics_pipeline_builder(device())
            .shader_stage()
                .vertex_shader(shader_source("movement_skybox.vert"))
                .fragment_shader(grid_shader_source())
            .vertex_input_state()
            .input_assembly_state()
                .triangles()
            .viewport_state()
                .viewport()
                    .origin(0, 0)
                    .dimension(projection_width, projection_height)
                    .min_depth(0.0F)
                    .max_depth(1.0F)
                .scissor()
                    .offset(0, 0)
                    .extent(projection_width, projection_height)
            .rasterization_state()
                .polygon_mode_fill()
                .cull_none()
            .multisample_state()
                .rasterization_samples(vkz::sample_count_x1)
            .color_blend_state()
                .attachment()
                    .add()
            .layout()
                .add_descriptor_set_layout(descriptor_layout_)
                .add_push_constant_range(vkz::shader_stage_fragment, 0, sizeof(push_constants))
            .dynamic_render_pass()
                .add_color_attachment(projection_format)
            .name("movement_floor")
            .build();
    floor_pipeline_.descriptor_sets = {descriptor_set_};
}

void movement_renderer::create_cube() {
    const auto cube = vkz::prim::cube();
    cube_vertex_buffer_ =
        vkz::buffer::builder(allocator_)
            .usage(vkz::buffer_usage_vertex)
            .memory_usage(VMA_MEMORY_USAGE_CPU_TO_GPU)
            .size(VKZ_BYTE_SIZE(cube.vertices))
            .build();
    auto vertex_mapping = cube_vertex_buffer_.map();
    std::memcpy(vertex_mapping._, cube.vertices.data(), static_cast<std::size_t>(cube_vertex_buffer_.create_info.size));
    vertex_mapping.unmap();

    cube_index_buffer_ =
        vkz::buffer::builder(allocator_)
            .usage(vkz::buffer_usage_index)
            .memory_usage(VMA_MEMORY_USAGE_CPU_TO_GPU)
            .size(VKZ_BYTE_SIZE(cube.indices))
            .build();
    auto index_mapping = cube_index_buffer_.map();
    std::memcpy(index_mapping._, cube.indices.data(), static_cast<std::size_t>(cube_index_buffer_.create_info.size));
    index_mapping.unmap();
    cube_index_count_ = static_cast<std::uint32_t>(cube.indices.size());
}

void movement_renderer::create_cube_pipeline() {
    cube_pipeline_ =
        vkz::make_graphics_pipeline_builder(device())
            .shader_stage()
                .vertex_shader(shader_source("movement_cube.vert"))
                .fragment_shader(shader_source("movement_cube.frag"))
            .vertex_input_state()
                .add_vertex_binding_description(0, sizeof(vkz::prim::vertex), VK_VERTEX_INPUT_RATE_VERTEX)
                .add_vertex_attribute_description(0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(vkz::prim::vertex, position))
                .add_vertex_attribute_description(1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(vkz::prim::vertex, normal))
            .input_assembly_state()
                .triangles()
            .viewport_state()
                .viewport()
                    .origin(0, 0)
                    .dimension(projection_width, projection_height)
                    .min_depth(0.0F)
                    .max_depth(1.0F)
                .scissor()
                    .offset(0, 0)
                    .extent(projection_width, projection_height)
            .rasterization_state()
                .polygon_mode_fill()
                .cull_back_face()
                .front_face_counter_clockwise()
            .multisample_state()
                .rasterization_samples(vkz::sample_count_x1)
            .color_blend_state()
                .attachment()
                    .add()
            .layout()
                .add_push_constant_range(vkz::shader_stage_vertex, 0, sizeof(object_push_constants))
            .dynamic_render_pass()
                .add_color_attachment(projection_format)
            .name("movement_cube")
            .build();
}

void movement_renderer::setup_projection_layer() {
    auto &swapchain = get_swapchain("movement_projection");
    projection_layer_ = vxr::make_struct<XrCompositionLayerProjection>();
    projection_layer_.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
    projection_layer_.views = projection_layer_views_.data();
    for (std::uint32_t view_index = 0; view_index < projection_layer_views_.size(); ++view_index) {
        auto &view = projection_layer_views_[view_index];
        view = vxr::make_struct<XrCompositionLayerProjectionView>();
        view.subImage.swapchain = swapchain.handle();
        view.subImage.imageRect.extent = {static_cast<std::int32_t>(swapchain.width()), static_cast<std::int32_t>(swapchain.height())};
        view.subImage.imageArrayIndex = view_index;
    }
}

void movement_renderer::render_projection_view(const vxr::frame_info &frame, std::uint32_t view_index) {
    const auto command_buffer = command_pool_.create_command_buffer();
    auto image = get_swapchain("movement_projection").images()[frame.image.image_index].image;
    prepare_render(command_buffer, image, view_index);

    vkz::render_info render_info;
    render_info.color_attachments.push_back({projection_views_[view_index][frame.image.image_index], projection_format, {0.0F, 0.0F, 0.0F, 0.0F}});
    render_info.render_area = {projection_width, projection_height};

    vkz::render(command_buffer, render_info, [&] {
        const auto view = camera_.world_pose(frame.view.views[view_index].pose);
        const auto view_projection = vxr::projection(frame.view.views[view_index].fov, 0.05F, 100.0F) * vxr::inverse(vxr::to_matrix(view));
        const push_constants constants{vxr::inverse(view_projection), vxr::vec4{view.position, 1.0F}, view_projection, vxr::vec4{teleport_.origin, 1.0F}, vxr::vec4{teleport_.velocity, 0.0F},
                                       vxr::vec4{teleport_.target, 1.0F}, teleport_.active && teleport_.valid ? 1U : 0U};
        vkz::bind_pipeline(command_buffer, skybox_pipeline_);
        vkz::push_constants(command_buffer, skybox_pipeline_, constants);
        vkz::draw(command_buffer, 3);
        vkz::bind_pipeline(command_buffer, floor_pipeline_);
        vkz::push_constants(command_buffer, floor_pipeline_, constants);
        vkz::draw(command_buffer, 3);

        const auto cube_model = vxr::translate(vxr::mat4{1.0F}, vxr::vec3{0.0F, 1.0F, -2.0F}) * vxr::scale(vxr::mat4{1.0F}, vxr::vec3{1.0F, 2.0F, 1.0F});
        vkz::bind_pipeline(command_buffer, cube_pipeline_);
        vkz::bind_vertex_buffer(command_buffer, cube_vertex_buffer_);
        vkz::bind_index_buffer<std::uint32_t>(command_buffer, cube_index_buffer_);
        vkz::push_constants(command_buffer, cube_pipeline_, object_push_constants{cube_model, view_projection});
        vkz::draw_indexed(command_buffer, cube_index_count_);
    });
    VKZ_CHECK_VULKAN(vkEndCommandBuffer(command_buffer));
    command_pool_.submit_and_wait(command_buffer);
    command_pool_.destroy(command_buffer);
}

movement_renderer::~movement_renderer() {
    cleanup();
}

void movement_renderer::prepare_render(VkCommandBuffer command_buffer, VkImage image, uint32_t view_index) {
    const VkImageSubresourceRange range{static_cast<VkImageAspectFlags>(vkz::image_aspect_color), 0, 1, view_index, 1};
    vkz::barrier::push_and_flush(command_buffer, image, range, vkz::pipeline_stage_none, vkz::pipeline_stage_color_attachment_output, vkz::access_none,
                                 vkz::access_color_attachment_write, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
}

int main(int argc, char **argv) {
    spdlog::info("{} movement (Vulkan header version {})", vxr::project_name(), vxr::vulkan_header_version());

    auto application  =
        vxr::application_builder{}
            .app_name("VulkanXR Movement")
            .vulkan_api_version(VK_API_VERSION_1_2)
            .openxr_api_version(XR_API_VERSION_1_0)
            .mirror()
                .extent(1280, 720)
                .multiview(true)
                .vsync(false)
                .eye_separation(0.08F)
                .barrel_distortion(0.2F)
            .session(movement_renderer::session_config())
            .renderer(movement_renderer::create)
            .mode(argc > 1 && std::string_view{argv[1]} == "--desktop" ? vxr::application_mode::desktop : vxr::application_mode::vr)
        .build();

    application->run();

    return EXIT_SUCCESS;
}
