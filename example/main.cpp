#include <vxr/vulkanxr.hpp>

#include <spdlog/spdlog.h>

#include <cstdlib>

int main() {
    spdlog::info("{} template (Vulkan header version {})",
                 vxr::project_name(), vxr::vulkan_header_version());
    spdlog::info("OpenXR API version {}",
                 vxr::version_to_string(vxr::openxr_api_version()));
    return EXIT_SUCCESS;
}
