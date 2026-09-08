#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <openxr/openxr.h>

#include <cmath>
#include <string>

namespace vxr {

    struct pose {
        glm::quat orientation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 position{0.0f};
    };

    struct space_location {
        std::string name;
        pose value;
    };

    struct transform {
        pose value;
        glm::vec3 scale{1.0f};

        [[nodiscard]] explicit operator glm::mat4() const noexcept {
            const glm::mat4 translation = glm::translate(glm::mat4{1.0f}, value.position);
            const glm::mat4 rotation = glm::mat4_cast(value.orientation);
            const glm::mat4 scaling = glm::scale(glm::mat4{1.0f}, scale);
            return translation * rotation * scaling;
        }
    };

    [[nodiscard]] inline pose to_pose(const XrPosef &value) noexcept {
        return {{value.orientation.w, value.orientation.x, value.orientation.y, value.orientation.z}, {value.position.x, value.position.y, value.position.z}};
    }

    [[nodiscard]] inline XrPosef to_xr_pose(const pose &value) noexcept {
        return {{value.orientation.x, value.orientation.y, value.orientation.z, value.orientation.w}, {value.position.x, value.position.y, value.position.z}};
    }

    [[nodiscard]] inline glm::mat4 to_matrix(const pose &value) noexcept {
        return glm::translate(glm::mat4{1.0f}, value.position) * glm::mat4_cast(value.orientation);
    }

    [[nodiscard]] inline glm::mat4 to_matrix(const XrPosef &value) noexcept {
        return to_matrix(to_pose(value));
    }

    [[nodiscard]] inline pose compose(const pose &parent, const pose &child) noexcept {
        return {glm::normalize(parent.orientation * child.orientation), parent.position + parent.orientation * child.position};
    }

    [[nodiscard]] inline pose inverse(const pose &value) noexcept {
        const auto orientation = glm::inverse(value.orientation);
        return {orientation, orientation * -value.position};
    }

    [[nodiscard]] inline glm::mat4 projection(const XrFovf &fov, float near_plane, float far_plane) noexcept {
        const float tan_left = std::tan(fov.angleLeft);
        const float tan_right = std::tan(fov.angleRight);
        const float tan_down = std::tan(fov.angleDown);
        const float tan_up = std::tan(fov.angleUp);
        const float width = tan_right - tan_left;
        const float height = tan_down - tan_up;

        glm::mat4 result{0.0F};
        result[0][0] = 2.0F / width;
        result[1][1] = 2.0F / height;
        result[2][0] = (tan_right + tan_left) / width;
        result[2][1] = (tan_up + tan_down) / height;
        result[2][2] = -far_plane / (far_plane - near_plane);
        result[2][3] = -1.0F;
        result[3][2] = -(far_plane * near_plane) / (far_plane - near_plane);
        return result;
    }

} // namespace vxr
