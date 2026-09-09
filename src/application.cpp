#include <vxr/application.hpp>

#include <vxr/openxr_helpers.hpp>
#include <vxr/session_service.hpp>
#include <vxr/status.hpp>

#include <spdlog/spdlog.h>

#include <exception>
#include <stdexcept>
#include <utility>

namespace vxr {

    application::application(context::builder &context_builder, session::config session_config, renderer_factory renderer_factory, mirror_config mirror_config)
        : context_builder_{context_builder}, session_config_{std::move(session_config)}, renderer_factory_{std::move(renderer_factory)}, mirror_config_{std::move(mirror_config)} {
        if (!mirror_config_.enabled) {
            return;
        }

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        window_.reset(glfwCreateWindow(static_cast<int>(mirror_config_.width), static_cast<int>(mirror_config_.height), mirror_config_.title.c_str(), nullptr, nullptr));
        if (!window_) {
            throw std::runtime_error{"Failed to create the VulkanXR mirror window"};
        }

        surface_provider_ = std::make_unique<vkz::glfw_surface_provider>(window_.get());
        context_builder_.surface(*surface_provider_);
        context_builder_.add_device_extension(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
        if (mirror_config_.multiview) {
            VkPhysicalDeviceMultiviewFeatures multiview{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES};
            multiview.multiview = VK_TRUE;
            context_builder_.add_extension(multiview);
        }
        std::uint32_t extension_count{};
        const char **extensions = glfwGetRequiredInstanceExtensions(&extension_count);
        if (!extensions) {
            throw std::runtime_error{"GLFW could not provide the Vulkan instance extensions required by the mirror"};
        }
        for (std::uint32_t index = 0; index < extension_count; ++index) {
            context_builder_.add_instance_extension(extensions[index]);
        }
    }

    void application::run() {
        while (!shutdown_requested_) {
            try {
                auto xr_context = context_builder_.build();
                backoff_.reset();

                rendering::context rendering_context{xr_context.vulkan_context(), xr_context.graphics_queue_family(), xr_context.graphics_queue()};
                auto renderer = renderer_factory_(rendering_context);
                if (!renderer) {
                    throw fatal_error{"The renderer factory returned a null renderer"};
                }

                std::unique_ptr<mirror> desktop_mirror;
                if (mirror_config_.enabled) {
                    desktop_mirror = std::make_unique<mirror>(window_.get(), rendering_context, mirror_config_);
                }

                session::service session_service{xr_context, rendering_context, session_config_, *renderer, desktop_mirror.get()};
                session_service.init();

                auto event = make_struct<XrEventDataBuffer>();
                while (!shutdown_requested_ && session_service.is_running()) {
                    if (desktop_mirror) {
                        desktop_mirror->poll_events();
                        if (desktop_mirror->should_close()) {
                            shutdown_requested_ = true;
                            session_service.terminate();
                            break;
                        }
                    }

                    while (poll_events(xr_context, event)) {
                        switch (event.type) {
                        case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED:
                            session_service.handle(reinterpret_cast<const XrEventDataSessionStateChanged &>(event));
                            break;
                        case XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING:
                            session_service.handle(*reinterpret_cast<const XrEventDataReferenceSpaceChangePending *>(&event));
                            break;
                        case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
                            session_service.terminate();
                            break;
                        default: {
                            char event_type_name[XR_MAX_STRUCTURE_NAME_SIZE]{};
                            const XrResult result = xrStructureTypeToString(xr_context.instance(), event.type, event_type_name);
                            if (XR_SUCCEEDED(result)) {
                                spdlog::warn("OpenXR event type {} ({}) is not handled", event_type_name, static_cast<std::int32_t>(event.type));
                            } else {
                                spdlog::warn("OpenXR event type {} is not handled", static_cast<std::int32_t>(event.type));
                            }
                            break;
                        }
                        }
                    }

                    session_service.process_frame();
                }

                const bool should_exit = shutdown_requested_ || session_service.should_exit();
                session_service.destroy();
                if (should_exit) break;
            } catch (const openxr_error &error) {
                spdlog::error("{}", error.what());
                if (is_client_error(error.result())) {
                    break;
                }
                backoff_();
            } catch (const retryable_error &error) {
                spdlog::error("{}", error.what());
                backoff_();
            } catch (const fatal_error &error) {
                spdlog::error("{}", error.what());
                break;
            } catch (const std::exception &error) {
                spdlog::error("{}", error.what());
                break;
            }
        }

        spdlog::info("OpenXR instance terminated, exiting application");
    }

    void application::shutdown() noexcept {
        shutdown_requested_ = true;
    }

    bool application::poll_events(const context &xr_context, XrEventDataBuffer &event) {
        event = make_struct<XrEventDataBuffer>();
        const XrResult result = xrPollEvent(xr_context.instance(), &event);
        if (result == XR_EVENT_UNAVAILABLE) {
            return false;
        }
        check(xr_context.instance(), result, "xrPollEvent");
        return true;
    }

} // namespace vxr
