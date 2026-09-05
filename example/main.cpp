#include <vxr/vxr.hpp>

#include "space_visualization_renderer.hpp"

#include <spdlog/spdlog.h>

#include <cstdlib>

int main() {
    spdlog::info("{} template (Vulkan header version {})", vxr::project_name(), vxr::vulkan_header_version());
    spdlog::info("OpenXR API version {}", vxr::version_to_string(vxr::openxr_api_version()));

    auto context_builder = vxr::context::create();
    context_builder
        .app_name("VulkanXR Space Visualization")
        .vulkan_api_version(VK_API_VERSION_1_2)
        .openxr_api_version(XR_API_VERSION_1_0);

    auto session_config = space_visualization_renderer::session_config();
    vxr::application application{context_builder, session_config, space_visualization_renderer::create};
    application.run();
    return EXIT_SUCCESS;
}
