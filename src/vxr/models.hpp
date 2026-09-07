#pragma once

#include <openxr/openxr.h>

#include <glm/vec2.hpp>

#include <compare>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace vxr {

    struct view_info {
        XrViewState state{};
        std::vector<XrView> views;
    };

    struct image_id {
        XrSwapchain swapchain{XR_NULL_HANDLE};
        std::uint32_t image_index{};
    };

    struct sub_image {
        std::string swapchain;
        glm::vec2 region{};
        std::uint32_t array_index{};
    };

    struct field_of_view {
        float angle_left{};
        float angle_right{};
        float angle_up{};
        float angle_down{};
    };

    struct frame_info {
        image_id image{};
        view_info view{};
        XrSpace space{XR_NULL_HANDLE};
        XrTime predicted_time{};
        XrDuration predicted_duration{};
        std::optional<image_id> depth_image;
    };

    using composition_layer =
            std::variant<XrCompositionLayerProjection *,
                         XrCompositionLayerQuad *,
                         XrCompositionLayerCubeKHR *,
                         XrCompositionLayerEquirectKHR *,
                         XrCompositionLayerCylinderKHR *,
                         XrCompositionLayerBaseHeader *>;

    struct layer {
        composition_layer value;
        std::uint32_t position{};

        [[nodiscard]] auto operator<=>(const layer &other) const noexcept {
            return position <=> other.position;
        }
    };

    using layers = std::vector<layer>;

} // namespace vxr
