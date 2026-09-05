#pragma once

#include <vxr/action.hpp>
#include <vxr/input.hpp>
#include <vxr/models.hpp>
#include <vxr/swapchain.hpp>

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace vxr::session {

    struct context {
        std::map<std::string, swapchain> swapchains;
        XrSessionState state{XR_SESSION_STATE_UNKNOWN};
        std::vector<XrView> views;
        XrSpace base_space{XR_NULL_HANDLE};
        std::map<std::string, XrSpace> spaces;
        std::map<std::string, action_set> action_sets;
        std::vector<XrActiveActionSet> active_action_sets;
        std::map<std::string, XrSpace> action_spaces;
        std::map<input::location, std::string> interaction_profiles;
        XrEnvironmentBlendMode blend_mode{XR_ENVIRONMENT_BLEND_MODE_OPAQUE};
        std::optional<XrEventDataReferenceSpaceChangePending> reference_space_change;
        bool cube_supported{};
        bool equirect_supported{};
        bool depth_supported{};
    };

} // namespace vxr::session
