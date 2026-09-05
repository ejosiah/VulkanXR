#pragma once

#include <volk.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

namespace vxr {

    class extension_dispatch {
      public:
        explicit extension_dispatch(XrInstance instance);

        [[nodiscard]] XrInstance instance() const noexcept {
            return instance_;
        }

        XrResult create_debug_utils_messenger(const XrDebugUtilsMessengerCreateInfoEXT &create_info, XrDebugUtilsMessengerEXT &messenger) const;

        XrResult destroy_debug_utils_messenger(XrDebugUtilsMessengerEXT messenger) const;

        XrResult set_debug_utils_object_name(const XrDebugUtilsObjectNameInfoEXT &name_info) const;

        XrResult submit_debug_utils_message(XrDebugUtilsMessageSeverityFlagsEXT severity, XrDebugUtilsMessageTypeFlagsEXT types, const XrDebugUtilsMessengerCallbackDataEXT &callback_data) const;

        XrResult session_begin_debug_utils_label_region(XrSession session, const XrDebugUtilsLabelEXT &label) const;

        XrResult session_end_debug_utils_label_region(XrSession session) const;

        XrResult session_insert_debug_utils_label(XrSession session, const XrDebugUtilsLabelEXT &label) const;

        XrResult get_vulkan_instance_extensions(XrSystemId system, uint32_t capacity, uint32_t &count, char *extensions) const;

        XrResult get_vulkan_device_extensions(XrSystemId system, uint32_t capacity, uint32_t &count, char *extensions) const;

        XrResult create_vulkan_instance(const XrVulkanInstanceCreateInfoKHR &create_info, VkInstance &vulkan_instance, VkResult &vulkan_result) const;

        XrResult create_vulkan_device(const XrVulkanDeviceCreateInfoKHR &create_info, VkDevice &vulkan_device, VkResult &vulkan_result) const;

        XrResult get_vulkan_graphics_device(const XrVulkanGraphicsDeviceGetInfoKHR &get_info, VkPhysicalDevice &physical_device) const;

        XrResult get_vulkan_graphics_requirements(XrSystemId system, XrGraphicsRequirementsVulkanKHR &requirements) const;

      private:
        template <typename Function> [[nodiscard]] Function load(const char *name) const;

        XrInstance instance_{};
        PFN_xrCreateDebugUtilsMessengerEXT create_debug_utils_messenger_{};
        PFN_xrDestroyDebugUtilsMessengerEXT destroy_debug_utils_messenger_{};
        PFN_xrSetDebugUtilsObjectNameEXT set_debug_utils_object_name_{};
        PFN_xrSubmitDebugUtilsMessageEXT submit_debug_utils_message_{};
        PFN_xrSessionBeginDebugUtilsLabelRegionEXT session_begin_debug_utils_label_region_{};
        PFN_xrSessionEndDebugUtilsLabelRegionEXT session_end_debug_utils_label_region_{};
        PFN_xrSessionInsertDebugUtilsLabelEXT session_insert_debug_utils_label_{};
        PFN_xrGetVulkanInstanceExtensionsKHR get_vulkan_instance_extensions_{};
        PFN_xrGetVulkanDeviceExtensionsKHR get_vulkan_device_extensions_{};
        PFN_xrCreateVulkanInstanceKHR create_vulkan_instance_{};
        PFN_xrCreateVulkanDeviceKHR create_vulkan_device_{};
        PFN_xrGetVulkanGraphicsDevice2KHR get_vulkan_graphics_device_{};
        PFN_xrGetVulkanGraphicsRequirements2KHR get_vulkan_graphics_requirements_{};
    };

} // namespace vxr
