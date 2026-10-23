#include <vxr/desktop_application.hpp>

#include <vxr/mirror.hpp>

#include <vulkanizer/log.hpp>
#include <vulkanizer/memory.hpp>

#include <stdexcept>
#include <utility>

namespace vxr {

    namespace {

        constexpr std::uint32_t desktop_image_count = 3;

        [[nodiscard]] vkz::image_usage image_usage(const swapchain_create_info &create_info) {
            vkz::image_usage result = vkz::image_usage_sampled;
            if ((create_info.usage_flags_ & XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT) != 0) {
                result |= vkz::image_usage_color;
            }
            if ((create_info.usage_flags_ & XR_SWAPCHAIN_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0) {
                result |= vkz::image_usage_depth_stencil;
            }
            if ((create_info.usage_flags_ & XR_SWAPCHAIN_USAGE_TRANSFER_SRC_BIT) != 0) {
                result |= vkz::image_usage_transfer_src;
            }
            if ((create_info.usage_flags_ & XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT) != 0) {
                result |= vkz::image_usage_transfer_dst;
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

    desktop_application::desktop_application(session::config session_config, renderer_factory renderer_factory, mirror_config mirror_config, vkz::builder vulkan_builder)
        : session_config_{std::move(session_config)}, renderer_factory_{std::move(renderer_factory)}, mirror_config_{std::move(mirror_config)}, vulkan_builder_{std::move(vulkan_builder)} {
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
        create_vulkan_context();
        create_rendering_context();
        create_swapchains();
        create_renderer();
        renderer_->init();

        while (!shutdown_requested_ && !mirror_->should_close()) {
            process_frame();
        }

        cleanup();
    }

    void desktop_application::shutdown() noexcept {
        shutdown_requested_ = true;
    }

    void desktop_application::create_vulkan_context() {
        auto &builder = vulkan_builder_;
        builder.app_name(mirror_config_.title).surface(*surface_provider_).add_queue(vkz::queue_graphics).add_device_extension(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
#ifndef NDEBUG
        builder.add_instance_extension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif
        std::uint32_t extension_count{};
        const char **extensions = glfwGetRequiredInstanceExtensions(&extension_count);
        for (std::uint32_t index = 0; index < extension_count; ++index) {
            builder.add_instance_extension(extensions[index]);
        }

        vulkan_context_ = builder.build();
    }

    void desktop_application::create_rendering_context() {
        const auto queue_family = vkz::find_graphics_present_queue_family(vulkan_context_.device.physical, vulkan_context_.surface);
        VkQueue graphics_queue{};
        vkGetDeviceQueue(vulkan_context_.device.logical, queue_family, 0, &graphics_queue);
        context_ = std::make_unique<rendering::context>(rendering::context{vulkan_context_, queue_family, graphics_queue});
        context_->state = XR_SESSION_STATE_FOCUSED;
        context_->views = desktop_views();
        context_->cube_supported = false;
        context_->equirect_supported = false;
        context_->depth_supported = true;
        context_->interaction_profiles[input::location::left_hand] = "/interaction_profiles/oculus/touch_controller";
        context_->interaction_profiles[input::location::right_hand] = "/interaction_profiles/oculus/touch_controller";
        allocator_ = vkz::vma_memory_allocator::create(vulkan_context_);
    }

    void desktop_application::create_swapchains() {
        for (const auto &specification : session_config_.swapchains) {
            auto &owned_images = images_[specification.name_];
            owned_images.reserve(desktop_image_count);
            std::vector<VkImage> handles;
            handles.reserve(desktop_image_count);
            for (std::uint32_t index = 0; index < desktop_image_count; ++index) {
                auto image = vkz::image::builder(allocator_)
                                 .flags(specification.face_count_ == 6 ? vkz::image_flags_cube_compatible : vkz::image_flags_none)
                                 .type(VK_IMAGE_TYPE_2D)
                                 .format(specification.format_)
                                 .extent(specification.width_, specification.height_)
                                 .mip_levels(specification.mip_count_)
                                 .array_layers(specification.array_size_ * specification.face_count_)
                                 .samples(static_cast<vkz::sample_count>(specification.sample_count_))
                                 .usage(image_usage(specification))
                                 .memory_usage(VMA_MEMORY_USAGE_GPU_ONLY)
                                 .build();
                handles.push_back(image.handle);
                owned_images.push_back(image);
            }
            context_->swapchains.try_emplace(specification.name_, specification, std::span<const VkImage>{handles});
        }
    }

    void desktop_application::create_renderer() {
        renderer_ = renderer_factory_(*context_);
        if (!renderer_) {
            throw std::runtime_error{"The desktop renderer factory returned a null renderer"};
        }
        mirror_ = std::make_unique<mirror>(window_.get(), *context_, mirror_config_);
        input_ = std::make_unique<desktop_input_adaptor>(window_.get(), session_config_);
    }

    void desktop_application::process_frame() {
        constexpr XrDuration frame_duration = 11'111'111;
        mirror_->poll_events();
        for (const auto &[name, actions] : input_->sync()) {
            renderer_->set(actions);
        }

        layers composition_layers;
        renderer_->begin_frame();
        for (auto &[name, swapchain] : context_->swapchains) {
            if (!swapchain.create_info().render_) continue;
            auto &image_index = image_indices_[swapchain.handle()];
            std::optional<image_id> depth_image;
            if (!swapchain.create_info().depth_.empty()) {
                auto &depth = context_->swapchains.at(swapchain.create_info().depth_);
                depth_image = image_id{depth.handle(), image_indices_[depth.handle()]};
            }
            renderer_->render({{swapchain.handle(), image_index}, {{XR_TYPE_VIEW_STATE}, context_->views}, XR_NULL_HANDLE, 0, frame_duration, depth_image}, composition_layers);
        }
        renderer_->end_frame();
        std::ranges::sort(composition_layers, {}, &layer::position);
        mirror_->render(composition_layers, image_indices_);
        for (auto &[handle, image_index] : image_indices_) {
            image_index = (image_index + 1) % desktop_image_count;
        }
    }

    void desktop_application::cleanup() noexcept {
        mirror_.reset();
        if (renderer_) {
            renderer_->cleanup();
        }
        renderer_.reset();
        input_.reset();
        for (auto &[name, swapchain] : context_->swapchains) {
            swapchain.destroy();
        }
        context_->swapchains.clear();
        for (auto &[name, owned_images] : images_) {
            for (auto &image : owned_images) {
                image.destroy();
            }
        }
        images_.clear();
        image_indices_.clear();
        allocator_.destroy();
        context_.reset();
        vulkan_context_ = {};
    }

} // namespace vxr
