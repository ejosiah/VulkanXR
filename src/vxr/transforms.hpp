#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <openxr/openxr.h>

namespace vxr {

    struct pose {
        glm::quat orientation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 position{0.0f};
    };

    [[nodiscard]] inline pose to_pose(const XrPosef &value) noexcept {
        return {{value.orientation.w, value.orientation.x, value.orientation.y, value.orientation.z}, {value.position.x, value.position.y, value.position.z}};
    }

    [[nodiscard]] inline XrPosef to_xr_pose(const pose &value) noexcept {
        return {{value.orientation.x, value.orientation.y, value.orientation.z, value.orientation.w}, {value.position.x, value.position.y, value.position.z}};
    }

    [[nodiscard]] inline glm::mat4 to_matrix(const pose &value) noexcept { return glm::translate(glm::mat4{1.0f}, value.position) * glm::mat4_cast(value.orientation); }

    [[nodiscard]] inline glm::mat4 to_matrix(const XrPosef &value) noexcept { return to_matrix(to_pose(value)); }

} // namespace vxr
