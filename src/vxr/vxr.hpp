#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include <openxr/openxr.h>

#include <vxr/application.hpp>
#include <vxr/backoff.hpp>
#include <vxr/extension_dispatch.hpp>
#include <vxr/font_pipeline.hpp>
#include <vxr/input.hpp>
#include <vxr/math.hpp>
#include <vxr/action.hpp>
#include <vxr/context.hpp>
#include <vxr/models.hpp>
#include <vxr/openxr_helpers.hpp>
#include <vxr/status.hpp>
#include <vxr/renderer.hpp>
#include <vxr/session_context.hpp>
#include <vxr/session_config.hpp>
#include <vxr/session_service.hpp>
#include <vxr/swapchain.hpp>
#include <vxr/transforms.hpp>

namespace vxr {

    [[nodiscard]] std::string_view project_name() noexcept;
    [[nodiscard]] std::uint32_t vulkan_header_version() noexcept;
    [[nodiscard]] XrVersion openxr_api_version() noexcept;
    [[nodiscard]] std::string version_to_string(XrVersion version);

} // namespace vxr
