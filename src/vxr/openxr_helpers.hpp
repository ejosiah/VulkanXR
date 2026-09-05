#pragma once

#include <vxr/status.hpp>

#include <openxr/openxr.h>
#include <openxr/openxr_reflection_structs.h>

#include <algorithm>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace vxr {

    template <typename T> struct structure_type;

#define VXR_DECLARE_STRUCTURE_TYPE(type, value)                                                                                                                                                        \
    template <> struct structure_type<type> {                                                                                                                                                          \
        static constexpr XrStructureType value_v = value;                                                                                                                                              \
    };
#define VXR_IGNORE_STRUCTURE_TYPE(type, value)
    XR_LIST_ALL_STRUCTURE_TYPES(VXR_DECLARE_STRUCTURE_TYPE, VXR_IGNORE_STRUCTURE_TYPE)
#undef VXR_IGNORE_STRUCTURE_TYPE
#undef VXR_DECLARE_STRUCTURE_TYPE

    template <typename T> [[nodiscard]] constexpr T make_struct() noexcept {
        T value{};
        value.type = structure_type<T>::value_v;
        value.next = nullptr;
        return value;
    }

    template <typename T, typename Function, typename... Args> [[nodiscard]] std::vector<T> enumerate(Function &&function, Args &&...args) {
        uint32_t count = 0;
        XrResult result = std::forward<Function>(function)(std::forward<Args>(args)..., 0, &count, nullptr);
        if (XR_FAILED(result)) {
            throw openxr_error{"OpenXR enumeration count", result};
        }

        std::vector<T> values(count);
        if constexpr (requires(T value) {
                          value.type;
                          value.next;
                      }) {
            std::ranges::fill(values, make_struct<T>());
        }
        result = std::forward<Function>(function)(std::forward<Args>(args)..., count, &count, values.data());
        if (XR_FAILED(result)) {
            throw openxr_error{"OpenXR enumeration", result};
        }
        values.resize(count);
        return values;
    }

    template <typename T, typename Function, typename... Args> [[nodiscard]] T get(Function &&function, Args &&...args) {
        T value = make_struct<T>();
        const XrResult result = std::forward<Function>(function)(std::forward<Args>(args)..., &value);
        if (XR_FAILED(result)) {
            throw openxr_error{"OpenXR getter", result};
        }
        return value;
    }

    [[nodiscard]] inline std::vector<XrViewConfigurationView> enumerate_view_configuration_views(XrInstance instance, XrSystemId system, XrViewConfigurationType view_configuration_type) {
        return enumerate<XrViewConfigurationView>(xrEnumerateViewConfigurationViews, instance, system, view_configuration_type);
    }

    [[nodiscard]] inline std::vector<XrView> locate_views(XrSession session, const XrViewLocateInfo &locate_info, XrViewState &view_state) {
        return enumerate<XrView>(xrLocateViews, session, &locate_info, &view_state);
    }

    [[nodiscard]] inline XrSpaceLocation locate_space(XrSpace space, XrSpace base_space, XrTime time) {
        return get<XrSpaceLocation>(xrLocateSpace, space, base_space, time);
    }

    [[nodiscard]] inline std::string_view to_string(XrViewConfigurationType type) noexcept {
        switch (type) {
        case XR_VIEW_CONFIGURATION_TYPE_PRIMARY_MONO:
            return "Mono";
        case XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO:
            return "Stereo";
        case XR_VIEW_CONFIGURATION_TYPE_PRIMARY_QUAD_VARJO:
            return "Quad";
        case XR_VIEW_CONFIGURATION_TYPE_SECONDARY_MONO_FIRST_PERSON_OBSERVER_MSFT:
            return "Mono first-person observer";
        default:
            return "Unknown view configuration";
        }
    }

    [[nodiscard]] inline std::string_view to_string(XrEnvironmentBlendMode mode) noexcept {
        switch (mode) {
        case XR_ENVIRONMENT_BLEND_MODE_OPAQUE:
            return "Opaque";
        case XR_ENVIRONMENT_BLEND_MODE_ADDITIVE:
            return "Additive";
        case XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND:
            return "Alpha blend";
        default:
            return "Unknown blend mode";
        }
    }

    [[nodiscard]] inline std::string_view to_string(XrSessionState state) noexcept {
        switch (state) {
        case XR_SESSION_STATE_IDLE:
            return "Idle";
        case XR_SESSION_STATE_READY:
            return "Ready";
        case XR_SESSION_STATE_SYNCHRONIZED:
            return "Synchronized";
        case XR_SESSION_STATE_VISIBLE:
            return "Visible";
        case XR_SESSION_STATE_FOCUSED:
            return "Focused";
        case XR_SESSION_STATE_STOPPING:
            return "Stopping";
        case XR_SESSION_STATE_LOSS_PENDING:
            return "Loss pending";
        case XR_SESSION_STATE_EXITING:
            return "Exiting";
        default:
            return "Unknown session state";
        }
    }

} // namespace vxr
