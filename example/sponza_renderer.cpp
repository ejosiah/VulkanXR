#include "sponza_renderer.hpp"

#include "sponza_lighting.hpp"
#include <NrcVk.h>

#include <vxr/vxr.hpp>

#include <vulkanizer/barrier.hpp>
#include <vulkanizer/render.hpp>
#include <vulkanizer/status.hpp>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <limits>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string_view>

namespace {

    constexpr VkFormat projection_format = VK_FORMAT_R8G8B8A8_SRGB;
    constexpr std::uint32_t projection_width = 2064;
    constexpr std::uint32_t projection_height = 2096;

    [[nodiscard]] std::filesystem::path resource_path(const std::string &name) {
        return std::filesystem::path{VULKANXR_RESOURCE_DIR} / name;
    }

    [[nodiscard]] vkz::buffer upload(vkz::vma_memory_allocator &allocator, vkz::buffer_usage usage, const void *data, std::size_t size) {
        auto buffer =
            vkz::buffer::builder(allocator)
                .usage(usage)
                .memory_usage(VMA_MEMORY_USAGE_CPU_TO_GPU)
                .size(size)
            .build();
        auto mapping = buffer.map();
        std::memcpy(mapping.as<void>(), data, size);
        buffer.flush();
        mapping.unmap();
        return buffer;
    }

} // namespace

sponza_renderer::sponza_renderer(vxr::rendering::context &context, const sponza_lighting_options &options)
: renderer{context}
, lighting_options_{options} {
}

void sponza_renderer::init() {
    // OpenXR runtimes may cap the API at 1.2; vkz uses the promoted core names.
    if (!vkCmdBeginRendering) vkCmdBeginRendering = vkCmdBeginRenderingKHR;
    if (!vkCmdEndRendering) vkCmdEndRendering = vkCmdEndRenderingKHR;
    if (!vkCmdPipelineBarrier2) vkCmdPipelineBarrier2 = vkCmdPipelineBarrier2KHR;
    if (!vkQueueSubmit2) vkQueueSubmit2 = vkQueueSubmit2KHR;
    if (!vkCmdBeginRendering || !vkCmdEndRendering || !vkCmdPipelineBarrier2 || !vkQueueSubmit2)
        throw std::runtime_error("Sponza requires dynamic rendering and synchronization2 entry points");

    allocator_ = vkz::vma_memory_allocator::create(vulkan_context(), VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT);
    command_pool_.init(device(), graphics_queue_family(), vkz::command_pool_flags_reset_command_buffer, graphics_queue());
    const auto scene_path = resource_path(lighting_options_.living_room ? "living_room/LivingRoom.vkmesh" : "sponza/Sponza.vkmesh");
    vkz::mesh::load_mesh_data(scene_path, scene_);
    auto material_path = scene_path;
    material_path.replace_extension(".vkmat");
    vkz::mesh::load_mesh_data_materials(material_path, scene_);
    if (scene_.instances.empty() || scene_.materials.empty())
        throw std::runtime_error("Scene requires mesh instances and materials");

    if (lighting_options_.living_room) {
        // Material 5 is the ceiling lamp shell in LivingRoom.vkmat.
        constexpr uint32_t ceiling_lamp_material = 5;
        auto &material = scene_.materials.at(ceiling_lamp_material);

        // Materials 20-22 are the three cushion fabrics in LivingRoom.vkmat.
        for (const uint32_t pillow_material : {20u, 21u, 22u}) {
            auto &pillow = scene_.materials.at(pillow_material);
            pillow.metallic_factor = material.metallic_factor;
            pillow.roughness_factor = material.roughness_factor;
            pillow.metallic_roughness_texture = material.metallic_roughness_texture;
        }

        // Identify the enclosed point light for the ceiling/standing light controls.
        vkz::mesh::light *lamp_light = nullptr;
        float nearest_distance = std::numeric_limits<float>::max();
        for (const auto &instance : scene_.instances) {
            const auto material_id = instance.material_id == vkz::mesh::inherit_material
                ? scene_.meshes.at(instance.mesh_id).material_id : instance.material_id;
            if (material_id != ceiling_lamp_material)
                continue;

            const auto bounds = scene_.boxes.at(instance.mesh_id).get_transformed(instance.model);
            const auto center = bounds.get_center();
            for (auto &light : scene_.lights) {
                if (light.type != vkz::mesh::light_type::point)
                    continue;

                const auto distance = glm::distance(center, light.position);
                if (distance < nearest_distance) {
                    nearest_distance = distance;
                    lamp_light = &light;
                }
            }
        }

        if (!lamp_light)
            throw std::runtime_error("Living room ceiling lamp requires an authored point light");

        for (auto &light : scene_.lights) {
            if (light.type != vkz::mesh::light_type::point)
                continue;

            const bool enabled = lighting_options_.lights
                && (&light == lamp_light ? lighting_options_.ceiling_light : lighting_options_.standing_light);
            if (!enabled)
                light.intensity = 0;

            spdlog::info("{} light: {}", &light == lamp_light ? "Ceiling" : "Standing", enabled ? "enabled" : "disabled");
        }

        const auto filter_file = cmrc::sponza_lighting_shaders::get_filesystem().open("tv_filter.comp.spv");
        std::vector<uint32_t> filter_shader(filter_file.size() / sizeof(uint32_t));
        std::memcpy(filter_shader.data(), filter_file.begin(), filter_file.size());
        tv_light_ = std::make_unique<rtx::tv_light>(allocator_, graphics_queue(), graphics_queue_family(),
            lighting_options_.video_path.empty() ? vkz::path{VULKANXR_CAT_VIDEO} : lighting_options_.video_path, filter_shader);
        tv_light_->configure(scene_);

        auto bounds = scene_.boxes.at(scene_.instances.front().mesh_id).get_transformed(scene_.instances.front().model);
        for (const auto &instance : scene_.instances) {
            const auto box = scene_.boxes.at(instance.mesh_id).get_transformed(instance.model);
            bounds.combine_point(box.min);
            bounds.combine_point(box.max);
        }
        camera_.update(context_.views);
        const auto center = bounds.get_center();
        camera_.teleport({center.x + bounds.get_size().x * 0.3f, 0, center.z});
        camera_.turn(glm::radians(90.0f));
    }

    const vkz::buffer_usage ray_usage = vkz::buffer_usage_acceleration_input;
    vertices_ = upload(allocator_, vkz::buffer_usage_vertex_storage | ray_usage, scene_.vertex_data.data(), scene_.vertex_data.size());
    indices_ = upload(allocator_, vkz::buffer_usage_index_storage | ray_usage, scene_.index_data.data(), scene_.index_data.size() * sizeof(std::uint32_t));
    create_projection_views();
    for (auto &renderer : scene_renderers_)
        renderer = std::make_unique<sponza_lighting>(vulkan_context(), allocator_, graphics_queue(), graphics_queue_family(), scene_,
            vertices_, indices_, glm::uvec2{projection_width, projection_height}, projection_format, lighting_options_, tv_light_.get());
    scene_.images.clear();
    scene_.images.shrink_to_fit();
    setup_projection_layer();
}

void sponza_renderer::cleanup() {
    if (!device()) {
        return;
    }
    vkz::batch_submission{graphics_queue()}.wait_idle();
    for (auto &renderer : scene_renderers_)
        renderer.reset();
    tv_light_.reset();

    for (auto &views : projection_views_) {
        for (auto &view : views) {
            view.destroy();
        }
        views.clear();
    }
    indices_.destroy();
    vertices_.destroy();
    command_pool_.deinit();
    allocator_.destroy();
}

std::vector<vxr::vibration> sponza_renderer::set(const vxr::action_set &actions) {
    update_camera_turn(actions);
    update_camera_movement(actions);
    return {};
}

void sponza_renderer::reference_space_changed(const XrEventDataReferenceSpaceChangePending &event) {
    camera_.reference_space_changed(event, context_.views);
}

void sponza_renderer::paused(const vxr::frame_info &frame, vxr::layers &layers) {
    render(frame, layers);
}

void sponza_renderer::render(const vxr::frame_info &frame, vxr::layers &layers) {
    if (frame.image.swapchain != get_swapchain("sponza_projection").handle()) {
        return;
    }

    if (tv_light_)
        tv_light_->new_frame(); // Decode once, then use the same frame in both eyes.

    frame_seconds_ = std::clamp(static_cast<float>(frame.predicted_duration) * 1.0e-9F, 0.0F, 0.1F);
    camera_.update(frame.view.views);
    const auto view_count = std::min(frame.view.views.size(), projection_layer_views_.size());
    for (std::uint32_t view_index = 0; view_index < view_count; ++view_index) {
        render_view(frame, view_index);
        projection_layer_views_[view_index].pose = frame.view.views[view_index].pose;
        projection_layer_views_[view_index].fov = frame.view.views[view_index].fov;
    }
    projection_layer_.viewCount = static_cast<std::uint32_t>(view_count);
    if (tv_light_ && view_count != 0)
        tv_light_->frame_submitted();

    layers.push_back({&projection_layer_, 0});
    if (frame_limit && ++frame_count_ >= frame_limit && request_shutdown) {
        if (lighting_options_.verify_lighting) {
            if (tv_light_)
                tv_light_->verify();

            for (auto &renderer : scene_renderers_)
                renderer->verify_lighting(lighting_options_);
        }
        request_shutdown();
    }
}

const char *sponza_renderer::name() const noexcept {
    return lighting_options_.living_room ? "Living room" : "Sponza";
}

std::unique_ptr<vxr::renderer> sponza_renderer::create(vxr::rendering::context &context, const sponza_lighting_options &options) {
    return std::make_unique<sponza_renderer>(context, options);
}

vxr::session::config sponza_renderer::session_config() {
    vxr::session::config config;
    config
        .stage()
        .add_action_set(
            vxr::session::action_set_specification{}
                .name("movement")
                .description("Sponza camera controls")
                .add_action("vive_right_turn_x", vxr::input::htc_vive::layout::right_trackpad_x, "Vive turn left and right")
                .add_action("vive_right_turn_click", vxr::input::htc_vive::layout::right_trackpad_click, "Vive turning activation")
                .add_action("vive_left_move_x", vxr::input::htc_vive::layout::left_trackpad_x, "Vive strafe left and right")
                .add_action("vive_left_move_y", vxr::input::htc_vive::layout::left_trackpad_y, "Vive move forward and backward")
                .add_action("vive_left_move_click", vxr::input::htc_vive::layout::left_trackpad_click, "Vive movement activation")
                .add_action("right_turn_x", vxr::input::oculus_touch::layout::right_thumbstick_x, "Turn left and right")
                .add_action("left_move_x", vxr::input::oculus_touch::layout::left_thumbstick_x, "Strafe left and right")
                .add_action("left_move_y", vxr::input::oculus_touch::layout::left_thumbstick_y, "Move forward and backward")
        )
        .add_swapchain(
            vxr::swapchain_create_info{}
                .name("sponza_projection")
                .usage()
                    .color_attachment()
                .format(projection_format)
                .extent(projection_width, projection_height)
                .array_size(2)
        );
    return config;
}

void sponza_renderer::create_projection_views() {
    auto &swapchain = get_swapchain("sponza_projection");
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

void sponza_renderer::setup_projection_layer() {
    auto &swapchain = get_swapchain("sponza_projection");
    projection_layer_ = vxr::make_struct<XrCompositionLayerProjection>();
    projection_layer_.views = projection_layer_views_.data();
    for (std::uint32_t view_index = 0; view_index < projection_layer_views_.size(); ++view_index) {
        auto &view = projection_layer_views_[view_index];
        view = vxr::make_struct<XrCompositionLayerProjectionView>();
        view.subImage.swapchain = swapchain.handle();
        view.subImage.imageRect.extent = {static_cast<std::int32_t>(swapchain.width()), static_cast<std::int32_t>(swapchain.height())};
        view.subImage.imageArrayIndex = view_index;
    }
}

void sponza_renderer::render_view(const vxr::frame_info &frame, std::uint32_t view_index) {
    auto &swapchain = get_swapchain("sponza_projection");
    const auto command_buffer = command_pool_.create_command_buffer();
    auto color_image = swapchain.images()[frame.image.image_index].image;
    const VkImageSubresourceRange color_range{static_cast<VkImageAspectFlags>(vkz::image_aspect_color), 0, 1, view_index, 1};
    vkz::barrier::push_and_flush(command_buffer, color_image, color_range, vkz::pipeline_stage_none, vkz::pipeline_stage_color_attachment_output, vkz::access_none,
                                 vkz::access_color_attachment_write, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    const auto view = camera_.world_pose(frame.view.views[view_index].pose);
    const auto view_matrix = vxr::inverse(vxr::to_matrix(view));
    const auto projection = vxr::projection(frame.view.views[view_index].fov, 0.05F, 1000.0F);
    scene_renderers_[view_index]->record(command_buffer, view_matrix, projection, view.position,
        projection_views_[view_index][frame.image.image_index], lighting_options_);
    command_pool_.submit_and_wait(command_buffer);
    scene_renderers_[view_index]->submitted();
    scene_renderers_[view_index]->end_frame();
}

void sponza_renderer::update_camera_movement(const vxr::action_set &actions) {
    const bool vive_active = vive_controller_active();
    const auto &move_x = actions.get(vive_active ? "vive_left_move_x" : "left_move_x");
    const auto &move_y = actions.get(vive_active ? "vive_left_move_y" : "left_move_y");
    const auto &move_click = actions.get("vive_left_move_click");
    const bool vive_trackpad_pressed = move_click.active && move_click.value<bool>();
    const bool movement_active = !vive_active || vive_trackpad_pressed;
    const float x = movement_active && move_x.active ? move_x.value<float>() : 0.0F;
    const float y = movement_active && move_y.active ? move_y.value<float>() : 0.0F;
    camera_.move({x, y}, 2.0F * frame_seconds_);
}

void sponza_renderer::update_camera_turn(const vxr::action_set &actions) {
    const bool vive_active = vive_controller_active();
    const auto &turn_x = actions.get(vive_active ? "vive_right_turn_x" : "right_turn_x");
    const auto &turn_click = actions.get("vive_right_turn_click");
    const bool vive_trackpad_pressed = turn_click.active && turn_click.value<bool>();
    const bool turning_active = !vive_active || vive_trackpad_pressed;
    const float x = turning_active && turn_x.active ? turn_x.value<float>() : 0.0F;
    if (std::abs(x) < 0.15F) {
        return;
    }
    camera_.turn(-x * 1.5F * frame_seconds_);
}

bool sponza_renderer::vive_controller_active() const {
    constexpr std::string_view vive_profile = "/interaction_profiles/htc/vive_controller";
    return std::ranges::any_of(context_.interaction_profiles, [](const auto &entry) {
        return entry.second == vive_profile;
    });
}

sponza_renderer::~sponza_renderer() {
    cleanup();
}

int main(int argc, char **argv) {
    spdlog::info("{} lighting demo (Vulkan header version {})", vxr::project_name(), vxr::vulkan_header_version());
    bool desktop{};
    bool validation{};
    uint32_t frame_limit{};
    sponza_lighting_options options;
#ifdef VULKANXR_LIVING_ROOM_EXAMPLE
    options.living_room = true;
#endif
    for (int index = 1; index < argc; ++index) {
        const std::string_view option{argv[index]};
        if (option == "--video-path") {
            if (++index >= argc || std::string_view(argv[index]).empty() || std::string_view(argv[index]).starts_with("--"))
                throw std::invalid_argument("--video-path requires a file path");

            options.video_path = argv[index];
        }
        else if (option == "--desktop") desktop = true;
        else if (option == "--living-room") options.living_room = true;
        else if (option == "--sponza") options.living_room = false;
        else if (option == "--validation") validation = true;
        else if (option == "--nrc") options.nrc = true;
        else if (option == "--ddgi") options.nrc = false;
        else if (option == "--nrd") options.nrd = true;
        else if (option == "--no-nrd") options.nrd = false;
        else if (option == "--verify-lighting") options.verify_lighting = true;
        else if (option == "--frames" && index + 1 < argc) frame_limit = uint32_t(std::stoul(argv[++index]));
        else if (option == "--full-resolution") options.full_resolution.fill(true);
        else if (option == "--full-resolution-gi") options.full_resolution[0] = true;
        else if (option == "--full-resolution-shadows") options.full_resolution[1] = true;
        else if (option == "--full-resolution-reflections") options.full_resolution[2] = true;
        else if (option == "--no-lights") options.lights = false;
        else if (option == "--no-ceiling-light") options.ceiling_light = false;
        else if (option == "--no-standing-light") options.standing_light = false;
        else if (option == "--no-gi") options.gi = false;
        else if (option == "--no-shadows") options.shadows = false;
        else if (option == "--no-reflections") options.reflections = false;
        else if (option == "--no-probe-offsetting") options.probe_offsetting = false;
        else if (option == "--visualize-probes") options.visualize_probes = true;
        else if (option == "--indirect-only") options.indirect_only = true;
        else if (option == "--reflection-only") options.reflection_only = true;
        else if (option == "--gi-multiplier") {
            if (++index >= argc)
                throw std::invalid_argument("--gi-multiplier requires a value");
            options.gi_multiplier = std::stof(argv[index]);
            if (!std::isfinite(options.gi_multiplier) || options.gi_multiplier < 0)
                throw std::invalid_argument("--gi-multiplier requires a finite, non-negative value");
        }
    }
    spdlog::info("Using raster {} with {}, ray-traced shadows and reflections; NRD {}",
        options.living_room ? "living room" : "Sponza", options.nrc ? "NRC" : "DDGI", options.nrd ? "enabled" : "disabled");

    vxr::application_builder builder;
    builder
        .app_name(options.living_room ? "VulkanXR Living Room" : "VulkanXR Sponza")
        .vulkan_api_version(VK_API_VERSION_1_2)
        .openxr_api_version(XR_API_VERSION_1_0)
        .enabled_features(VkPhysicalDeviceFeatures{.shaderStorageImageExtendedFormats = VK_TRUE,
            .shaderStorageImageReadWithoutFormat = VK_TRUE, .shaderStorageImageWriteWithoutFormat = VK_TRUE})
        .add_device_extension(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME)
        .add_device_extension(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME)
        .add_device_extension(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME)
        .add_device_extension(VK_KHR_RAY_QUERY_EXTENSION_NAME)
        .add_device_extension(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME)
        .add_device_extension(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);
    if (options.living_room) {
        builder.add_unique_queue(vkz::queue_video_decode);
        for (const auto extension : vkz::video::required_extensions())
            builder.add_device_extension(extension);
        builder.add_extension(VkPhysicalDeviceSamplerYcbcrConversionFeatures{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_YCBCR_CONVERSION_FEATURES, nullptr, VK_TRUE});
    }

    if (validation) builder.add_instance_layer("VK_LAYER_KHRONOS_validation");
    VkPhysicalDeviceBufferDeviceAddressFeatures address{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES};
    address.bufferDeviceAddress = VK_TRUE;
    VkPhysicalDeviceAccelerationStructureFeaturesKHR acceleration{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
    acceleration.accelerationStructure = VK_TRUE;
    VkPhysicalDeviceDescriptorIndexingFeatures indexing{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES};
    indexing.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
    VkPhysicalDeviceRayTracingPipelineFeaturesKHR tracing{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR};
    tracing.rayTracingPipeline = VK_TRUE;
    VkPhysicalDeviceRayQueryFeaturesKHR query{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};
    query.rayQuery = VK_TRUE;
    VkPhysicalDeviceScalarBlockLayoutFeatures scalar{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SCALAR_BLOCK_LAYOUT_FEATURES};
    scalar.scalarBlockLayout = VK_TRUE;
    VkPhysicalDeviceDynamicRenderingFeatures rendering{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES};
    rendering.dynamicRendering = VK_TRUE;
    VkPhysicalDeviceSynchronization2Features sync{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES};
    sync.synchronization2 = VK_TRUE;
    builder
        .add_extension(address)
        .add_extension(acceleration)
        .add_extension(indexing)
        .add_extension(tracing)
        .add_extension(query)
        .add_extension(scalar)
        .add_extension(sync);
    // The OpenXR device factory already supplies this feature in VR mode.
    if (desktop) builder.add_extension(rendering);
    if (options.nrc) {
        const char *const *extensions;
        auto count = nrc::vulkan::GetVulkanDeviceExtensions(extensions);
        for (uint32_t i = 0; i < count; ++i) builder.add_device_extension(extensions[i]);
        count = nrc::vulkan::GetVulkanInstanceExtensions(extensions);
        for (uint32_t i = 0; i < count; ++i) builder.add_instance_extension(extensions[i]);
    }
    VkPhysicalDeviceTimelineSemaphoreFeatures timeline{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES};
    timeline.timelineSemaphore = VK_TRUE;
    VkPhysicalDeviceUniformBufferStandardLayoutFeatures uniform{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_UNIFORM_BUFFER_STANDARD_LAYOUT_FEATURES};
    uniform.uniformBufferStandardLayout = VK_TRUE;
    builder
        .add_extension(timeline)
        .add_extension(uniform);

    std::unique_ptr<vxr::app> application;
    application =
        builder
            .mirror()
                .extent(1280, 720)
                .multiview(true)
                .vsync(false)
                .eye_separation(0.08F)
                .barrel_distortion(0.2F)
            .session(sponza_renderer::session_config())
            .renderer([&](vxr::rendering::context &context) {
                auto renderer = std::make_unique<sponza_renderer>(context, options);
                renderer->frame_limit = frame_limit;
                renderer->request_shutdown = [&] { application->shutdown(); };
                return renderer;
            })
            .mode(desktop ? vxr::application_mode::desktop : vxr::application_mode::vr)
        .build();
    application->run();
    return EXIT_SUCCESS;
}
