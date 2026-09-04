#include <vxr/extension_dispatch.hpp>

namespace vxr {

    template <typename Function> Function extension_dispatch::load(const char *name) const {
        Function function = nullptr;
        const XrResult result = xrGetInstanceProcAddr(instance_, name, reinterpret_cast<PFN_xrVoidFunction *>(&function));
        return XR_SUCCEEDED(result) ? function : nullptr;
    }

    extension_dispatch::extension_dispatch(XrInstance instance) : instance_{instance} {
        create_debug_utils_messenger_ = load<PFN_xrCreateDebugUtilsMessengerEXT>("xrCreateDebugUtilsMessengerEXT");
        destroy_debug_utils_messenger_ = load<PFN_xrDestroyDebugUtilsMessengerEXT>("xrDestroyDebugUtilsMessengerEXT");
        set_debug_utils_object_name_ = load<PFN_xrSetDebugUtilsObjectNameEXT>("xrSetDebugUtilsObjectNameEXT");
        submit_debug_utils_message_ = load<PFN_xrSubmitDebugUtilsMessageEXT>("xrSubmitDebugUtilsMessageEXT");
        session_begin_debug_utils_label_region_ = load<PFN_xrSessionBeginDebugUtilsLabelRegionEXT>("xrSessionBeginDebugUtilsLabelRegionEXT");
        session_end_debug_utils_label_region_ = load<PFN_xrSessionEndDebugUtilsLabelRegionEXT>("xrSessionEndDebugUtilsLabelRegionEXT");
        session_insert_debug_utils_label_ = load<PFN_xrSessionInsertDebugUtilsLabelEXT>("xrSessionInsertDebugUtilsLabelEXT");
        get_vulkan_instance_extensions_ = load<PFN_xrGetVulkanInstanceExtensionsKHR>("xrGetVulkanInstanceExtensionsKHR");
        get_vulkan_device_extensions_ = load<PFN_xrGetVulkanDeviceExtensionsKHR>("xrGetVulkanDeviceExtensionsKHR");
        create_vulkan_instance_ = load<PFN_xrCreateVulkanInstanceKHR>("xrCreateVulkanInstanceKHR");
        create_vulkan_device_ = load<PFN_xrCreateVulkanDeviceKHR>("xrCreateVulkanDeviceKHR");
        get_vulkan_graphics_device_ = load<PFN_xrGetVulkanGraphicsDevice2KHR>("xrGetVulkanGraphicsDevice2KHR");
        get_vulkan_graphics_requirements_ = load<PFN_xrGetVulkanGraphicsRequirements2KHR>("xrGetVulkanGraphicsRequirements2KHR");
    }

#define VXR_CALL(function, ...) (function ? function(__VA_ARGS__) : XR_ERROR_FUNCTION_UNSUPPORTED)

    XrResult extension_dispatch::create_debug_utils_messenger(const XrDebugUtilsMessengerCreateInfoEXT &info, XrDebugUtilsMessengerEXT &messenger) const {
        return VXR_CALL(create_debug_utils_messenger_, instance_, &info, &messenger);
    }

    XrResult extension_dispatch::destroy_debug_utils_messenger(XrDebugUtilsMessengerEXT messenger) const { return VXR_CALL(destroy_debug_utils_messenger_, messenger); }

    XrResult extension_dispatch::set_debug_utils_object_name(const XrDebugUtilsObjectNameInfoEXT &info) const { return VXR_CALL(set_debug_utils_object_name_, instance_, &info); }

    XrResult extension_dispatch::submit_debug_utils_message(XrDebugUtilsMessageSeverityFlagsEXT severity, XrDebugUtilsMessageTypeFlagsEXT types,
                                                            const XrDebugUtilsMessengerCallbackDataEXT &data) const {
        return VXR_CALL(submit_debug_utils_message_, instance_, severity, types, &data);
    }

    XrResult extension_dispatch::session_begin_debug_utils_label_region(XrSession session, const XrDebugUtilsLabelEXT &label) const {
        return VXR_CALL(session_begin_debug_utils_label_region_, session, &label);
    }

    XrResult extension_dispatch::session_end_debug_utils_label_region(XrSession session) const { return VXR_CALL(session_end_debug_utils_label_region_, session); }

    XrResult extension_dispatch::session_insert_debug_utils_label(XrSession session, const XrDebugUtilsLabelEXT &label) const { return VXR_CALL(session_insert_debug_utils_label_, session, &label); }

    XrResult extension_dispatch::get_vulkan_instance_extensions(XrSystemId system, uint32_t capacity, uint32_t &count, char *extensions) const {
        return VXR_CALL(get_vulkan_instance_extensions_, instance_, system, capacity, &count, extensions);
    }

    XrResult extension_dispatch::get_vulkan_device_extensions(XrSystemId system, uint32_t capacity, uint32_t &count, char *extensions) const {
        return VXR_CALL(get_vulkan_device_extensions_, instance_, system, capacity, &count, extensions);
    }

    XrResult extension_dispatch::create_vulkan_instance(const XrVulkanInstanceCreateInfoKHR &info, VkInstance &instance, VkResult &result) const {
        return VXR_CALL(create_vulkan_instance_, instance_, &info, &instance, &result);
    }

    XrResult extension_dispatch::create_vulkan_device(const XrVulkanDeviceCreateInfoKHR &info, VkDevice &device, VkResult &result) const {
        return VXR_CALL(create_vulkan_device_, instance_, &info, &device, &result);
    }

    XrResult extension_dispatch::get_vulkan_graphics_device(const XrVulkanGraphicsDeviceGetInfoKHR &info, VkPhysicalDevice &device) const {
        return VXR_CALL(get_vulkan_graphics_device_, instance_, &info, &device);
    }

    XrResult extension_dispatch::get_vulkan_graphics_requirements(XrSystemId system, XrGraphicsRequirementsVulkanKHR &requirements) const {
        return VXR_CALL(get_vulkan_graphics_requirements_, instance_, system, &requirements);
    }

#undef VXR_CALL

} // namespace vxr
