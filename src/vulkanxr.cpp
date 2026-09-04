#include <vxr/vulkanxr.hpp>

#include <vulkan/vulkan_core.h>

namespace vxr {

    std::string_view project_name() noexcept {
        return "VulkanXR";
    }

    std::uint32_t vulkan_header_version() noexcept {
        return VK_HEADER_VERSION_COMPLETE;
    }

XrVersion openxr_api_version() noexcept {
    return XR_CURRENT_API_VERSION;
}

std::string version_to_string(const XrVersion version) {
    return std::to_string(XR_VERSION_MAJOR(version)) + "."
        + std::to_string(XR_VERSION_MINOR(version)) + "."
        + std::to_string(XR_VERSION_PATCH(version));
}

} // namespace vxr
