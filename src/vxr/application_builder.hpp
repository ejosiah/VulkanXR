#pragma once

#include <vxr/app.hpp>
#include <vxr/application.hpp>
#include <vxr/desktop_application.hpp>

#include <memory>
#include <optional>

namespace vxr {

    class application_builder {
      public:
        application_builder() = default;

        application_builder &app_name(const std::string &name);

        application_builder &app_version(const std::string &version);

        application_builder &engine_name(const std::string &name);

        application_builder &engine_version(const std::string &version);

        application_builder &vulkan_api_version(std::uint32_t version);

        application_builder &openxr_api_version(XrVersion version);

        application_builder &form_factor(XrFormFactor form_factor);

        application_builder &add_openxr_extension(const std::string &extension);

        application_builder &add_optional_openxr_extension(const std::string &extension);

        application_builder &add_openxr_layer(const std::string &layer);

        application_builder &add_instance_extension(const std::string &extension);

        application_builder &add_instance_layer(const std::string &layer);

        application_builder &add_device_extension(const std::string &extension);

        application_builder &add_device_layer(const std::string &layer);

        application_builder &enabled_features(const VkPhysicalDeviceFeatures &features);

        application_builder &surface(const vkz::surface_provider &surface_provider);

        application_builder &add_queue(VkQueueFlagBits flags);

        application_builder &add_unique_queue(VkQueueFlagBits flags);

        application_builder &num_graphics_queues(std::uint32_t count);

        application_builder &add_extension_chain(const vkz::device_extension_chain &extensions);

        template <vkz::vulkan_structure T> application_builder &add_extension(const T &extension) {
            context_builder_.add_extension(extension);
            return *this;
        }

        application_builder &session(session::config session_config);

        application_builder &renderer(renderer_factory renderer_factory);

        application_builder &mode(application_mode mode);

        [[nodiscard]] std::unique_ptr<app> build();

      private:
        context::builder context_builder_;
        std::optional<session::config> session_config_;
        renderer_factory renderer_factory_;
        application_mode mode_{application_mode::vr};
    };

} // namespace vxr
