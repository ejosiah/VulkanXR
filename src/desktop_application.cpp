#include <vxr/desktop_application.hpp>

#include <vxr/mirror.hpp>

#include <vulkanizer/log.hpp>
#include <vulkanizer/memory.hpp>

#include <chrono>
#include <stdexcept>
#include <utility>

namespace vxr {

    namespace {

        constexpr std::uint32_t desktop_image_count = 3;

        [[nodiscard]] VkImageUsageFlags image_usage(const swapchain_create_info &create_info) {
            VkImageUsageFlags result = VK_IMAGE_USAGE_SAMPLED_BIT;
            if ((create_info.usage_flags_ & XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT) != 0) {
                result |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
            }
            if ((create_info.usage_flags_ & XR_SWAPCHAIN_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0) {
                result |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
            }
            if ((create_info.usage_flags_ & XR_SWAPCHAIN_USAGE_TRANSFER_SRC_BIT) != 0) {
                result |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
            }
            if ((create_info.usage_flags_ & XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT) != 0) {
                result |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            }
            return result;
        }

        [[nodiscard]] std::vector<XrView> desktop_views() {
            std::vector<XrView> views(2);
            for (std::uint32_t index = 0; index < views.size(); ++index) {
                views[index] = {XR_TYPE_VIEW};
                views[index].pose.orientation.w = 1.0F;
                views[index].pose.position = {index == 0 ? -0.032F : 0.032F, 1.7F, 0.0F};
                views[index].fov = {-0.785398F, 0.785398F, 0.785398F, -0.785398F};
            }
            return views;
        }

    } // namespace

    desktop_application::desktop_application(const session::config &session_config, renderer_factory renderer_factory, mirror_config mirror_config)
        : session_config_{session_config}, renderer_factory_{std::move(renderer_factory)}, mirror_config_{std::move(mirror_config)} {
        session_config_.validate();
        vkz::spdlog_adapter::install();
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        window_.reset(glfwCreateWindow(static_cast<int>(mirror_config_.width), static_cast<int>(mirror_config_.height), mirror_config_.title.c_str(), nullptr, nullptr));
        if (!window_) {
            throw std::runtime_error{"Failed to create the VulkanXR desktop window"};
        }
        surface_provider_ = std::make_unique<vkz::glfw_surface_provider>(window_.get());
    }

    void desktop_application::run() {
        auto builder = vkz::context::builder();
        builder
            .app_name(mirror_config_.title)
            .api_version(VK_API_VERSION_1_3)
            .surface(*surface_provider_)
            .add_queue(VK_QUEUE_GRAPHICS_BIT)
            .add_device_extension(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
#ifndef NDEBUG
        builder.add_instance_extension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif
        std::uint32_t extension_count{};
        const char **extensions = glfwGetRequiredInstanceExtensions(&extension_count);
        for (std::uint32_t index = 0; index < extension_count; ++index) {
            builder.add_instance_extension(extensions[index]);
        }

        auto vulkan_context = builder.build();
        const auto queue_family = vkz::find_graphics_present_queue_family(vulkan_context.device.physical, vulkan_context.surface);
        VkQueue graphics_queue{};
        vkGetDeviceQueue(vulkan_context.device.logical, queue_family, 0, &graphics_queue);
        desktop_context rendering_context{std::move(vulkan_context), queue_family, graphics_queue};
        auto allocator = vkz::vma_memory_allocator::create(rendering_context.vulkan_context());
        session::context session_context;
        session_context.state = XR_SESSION_STATE_FOCUSED;
        session_context.views = desktop_views();
        session_context.cube_supported = true;
        session_context.equirect_supported = true;
        session_context.depth_supported = true;
        session_context.interaction_profiles[input::location::left_hand] = "/interaction_profiles/oculus/touch_controller";
        session_context.interaction_profiles[input::location::right_hand] = "/interaction_profiles/oculus/touch_controller";

        std::map<std::string, std::vector<vkz::image>> images;
        for (const auto &specification : session_config_.swapchains) {
            auto &owned_images = images[specification.name_];
            owned_images.reserve(desktop_image_count);
            std::vector<VkImage> handles;
            handles.reserve(desktop_image_count);
            for (std::uint32_t index = 0; index < desktop_image_count; ++index) {
                auto image =
                    vkz::image::builder(allocator)
                        .flags(specification.face_count_ == 6 ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0)
                        .type(VK_IMAGE_TYPE_2D)
                        .format(specification.format_)
                        .extent(specification.width_, specification.height_)
                        .mip_levels(specification.mip_count_)
                        .array_layers(specification.array_size_ * specification.face_count_)
                        .samples(static_cast<VkSampleCountFlagBits>(specification.sample_count_))
                        .usage(image_usage(specification))
                        .memory_usage(VMA_MEMORY_USAGE_GPU_ONLY)
                        .build();
                handles.push_back(image.handle);
                owned_images.push_back(image);
            }
            session_context.swapchains.try_emplace(specification.name_, specification, std::span<const VkImage>{handles});
        }

        auto renderer = renderer_factory_(rendering_context, session_context);
        if (!renderer) {
            throw std::runtime_error{"The desktop renderer factory returned a null renderer"};
        }
        auto desktop_mirror = std::make_unique<mirror>(window_.get(), rendering_context, session_context, mirror_config_);
        desktop_input_adaptor input{window_.get(), session_config_};
        renderer->init();
        std::map<XrSwapchain, std::uint32_t> image_indices;
        const auto frame_duration = std::chrono::nanoseconds{11'111'111};

        while (!shutdown_requested_ && !desktop_mirror->should_close()) {
            desktop_mirror->poll_events();
            for (const auto &[name, actions] : input.sync()) {
                renderer->set(actions);
            }

            layers composition_layers;
            renderer->begin_frame();
            for (auto &[name, swapchain] : session_context.swapchains) {
                if (!swapchain.create_info().render_) continue;
                auto &image_index = image_indices[swapchain.handle()];
                std::optional<image_id> depth_image;
                if (!swapchain.create_info().depth_.empty()) {
                    auto &depth = session_context.swapchains.at(swapchain.create_info().depth_);
                    depth_image = image_id{depth.handle(), image_indices[depth.handle()]};
                }
                renderer->render({{swapchain.handle(), image_index}, {{XR_TYPE_VIEW_STATE}, session_context.views}, XR_NULL_HANDLE, 0, frame_duration.count(), depth_image}, composition_layers);
            }
            renderer->end_frame();
            std::ranges::sort(composition_layers, {}, &layer::position);
            desktop_mirror->render(composition_layers, image_indices);
            for (auto &[handle, image_index] : image_indices) {
                image_index = (image_index + 1) % desktop_image_count;
            }
        }

        desktop_mirror.reset();
        renderer->cleanup();
        renderer.reset();
        for (auto &[name, swapchain] : session_context.swapchains) {
            swapchain.destroy();
        }
        session_context.swapchains.clear();
        for (auto &[name, owned_images] : images) {
            for (auto &image : owned_images) {
                image.destroy();
            }
        }
        allocator.destroy();
    }

    void desktop_application::shutdown() noexcept {
        shutdown_requested_ = true;
    }

} // namespace vxr
