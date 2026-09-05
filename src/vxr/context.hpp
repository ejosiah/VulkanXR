#pragma once

#include <openxr/openxr.h>
#include <vulkanizer/context.hpp>
#include <openxr/openxr_platform.h>

#include <cstdint>
#include <string>
#include <vector>

namespace vxr {

    class context {
      public:
        class builder;

        context() = default;

        ~context();

        context(const context &) = delete;

        context &operator=(const context &) = delete;

        context(context &&other) noexcept;

        context &operator=(context &&other) noexcept;

        [[nodiscard]] static builder create();

        [[nodiscard]] XrInstance instance() const noexcept {
            return instance_;
        }

        [[nodiscard]] XrSystemId system_id() const noexcept {
            return system_id_;
        }

        [[nodiscard]] VkInstance vulkan_instance() const noexcept {
            return vulkan_.instance;
        }

        [[nodiscard]] VkPhysicalDevice physical_device() const noexcept {
            return vulkan_.device.physical;
        }

        [[nodiscard]] vkz::device &device() noexcept {
            return vulkan_.device;
        }

        [[nodiscard]] const vkz::device &device() const noexcept {
            return vulkan_.device;
        }

        [[nodiscard]] VkSurfaceKHR surface() const noexcept {
            return vulkan_.surface;
        }

        [[nodiscard]] VkDebugUtilsMessengerEXT debug_messenger() const noexcept {
            return vulkan_.debug_messenger;
        }

        [[nodiscard]] std::uint32_t vulkan_api_version() const noexcept {
            return vulkan_.api_version;
        }

        [[nodiscard]] std::uint32_t graphics_queue_family() const noexcept {
            return graphics_queue_family_;
        }

        [[nodiscard]] VkQueue graphics_queue() const noexcept {
            return graphics_queue_;
        }

        [[nodiscard]] XrGraphicsBindingVulkan2KHR graphics_binding() const noexcept;

      private:
        friend class builder;

        context(XrInstance instance, XrSystemId system_id, XrDebugUtilsMessengerEXT debug_messenger, vkz::context vulkan, std::uint32_t graphics_queue_family, VkQueue graphics_queue) noexcept;

        void reset() noexcept;

        XrInstance instance_{XR_NULL_HANDLE};
        XrSystemId system_id_{XR_NULL_SYSTEM_ID};
        XrDebugUtilsMessengerEXT openxr_debug_messenger_{XR_NULL_HANDLE};
        vkz::context vulkan_{};
        std::uint32_t graphics_queue_family_{};
        VkQueue graphics_queue_{VK_NULL_HANDLE};
    };

    class context::builder : public vkz::builder {
      public:
        builder();

        builder &app_name(const std::string &name);

        builder &vulkan_api_version(std::uint32_t version);

        builder &openxr_api_version(XrVersion version);

        builder &form_factor(XrFormFactor form_factor);

        builder &add_openxr_extension(const std::string &extension);

        builder &add_openxr_layer(const std::string &layer);

        [[nodiscard]] context build();

      private:
        std::string application_name_{"VulkanXR"};
        std::uint32_t vulkan_api_version_{VK_API_VERSION_1_3};
        XrVersion openxr_api_version_{XR_CURRENT_API_VERSION};
        XrFormFactor form_factor_{XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY};
        std::vector<std::string> openxr_extensions_;
        std::vector<std::string> openxr_layers_;
    };

} // namespace vxr
