#include <vxr/vxr.hpp>

#include <spdlog/spdlog.h>

#include <cassert>

int main() {

    uint32_t vk_version = VK_API_VERSION_1_3;

    try {
        auto context =
                vxr::context::create().app_name("VulkanXR context test").vulkan_api_version(vk_version).openxr_api_version(XR_CURRENT_API_VERSION).form_factor(XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY).build();

        assert(context.instance() != XR_NULL_HANDLE);
        assert(context.system_id() != XR_NULL_SYSTEM_ID);
        assert(context.vulkan_instance() != VK_NULL_HANDLE);
        assert(context.physical_device() != VK_NULL_HANDLE);
        assert(context.device().logical != VK_NULL_HANDLE);
        assert(context.vulkan_api_version() == vk_version);

        auto properties = vxr::make_struct<XrInstanceProperties>();
        const auto result = xrGetInstanceProperties(context.instance(), &properties);
        assert(XR_SUCCEEDED(result));
        spdlog::info("OpenXR runtime: {}, version {}", properties.runtimeName, vxr::version_to_string(properties.runtimeVersion));

        const auto binding = context.graphics_binding();
        assert(binding.instance == context.vulkan_instance());
        assert(binding.physicalDevice == context.physical_device());
        assert(binding.device == context.device().logical);
        return 0;
    } catch (const std::exception &error) {
        spdlog::error("Context test failed: {}", error.what());
        return 1;
    }
}
