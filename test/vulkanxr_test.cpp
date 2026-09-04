#include <vxr/vulkanxr.hpp>

#include <cassert>
#include <type_traits>

int main() {
    static_assert(std::is_base_of_v<vkz::builder, vxr::context::builder>);
    static_assert(!std::is_base_of_v<vkz::context, vxr::context>);
    assert(vxr::project_name() == "VulkanXR");
    assert(vxr::vulkan_header_version() != 0);
    assert(vxr::openxr_api_version() >= XR_MAKE_VERSION(1, 1, 0));
    assert(vxr::version_to_string(XR_MAKE_VERSION(1, 2, 3)) == "1.2.3");
    return 0;
}
