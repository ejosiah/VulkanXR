#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include <openxr/openxr.h>

#include <vxr/extension_dispatch.hpp>
#include <vxr/context.hpp>
#include <vxr/openxr_helpers.hpp>
#include <vxr/transforms.hpp>

namespace vxr {

[[nodiscard]] std::string_view project_name() noexcept;
[[nodiscard]] std::uint32_t vulkan_header_version() noexcept;
[[nodiscard]] XrVersion openxr_api_version() noexcept;
[[nodiscard]] std::string version_to_string(XrVersion version);

} // namespace vxr
