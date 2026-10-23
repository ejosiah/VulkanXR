#pragma once

#include <vxr/transforms.hpp>

#include <openxr/openxr.h>

#include <vector>

namespace vxr {

    class camera {
      public:
        void update(const std::vector<XrView> &views);

        void reference_space_changed(const XrEventDataReferenceSpaceChangePending &event, const std::vector<XrView> &views);

        void move(const vec2 &direction, float distance);

        void turn(float angle);

        void teleport(const vec3 &target);

        [[nodiscard]] pose world_pose(const pose &tracking_pose) const noexcept;

        [[nodiscard]] pose world_pose(const XrPosef &tracking_pose) const noexcept;

        [[nodiscard]] const vec3 &position() const noexcept;

        [[nodiscard]] const quat &orientation() const noexcept;

        [[nodiscard]] bool valid() const noexcept;

      private:
        [[nodiscard]] static pose head_pose(const std::vector<XrView> &views);

        pose world_from_tracking_{};
        pose tracked_head_in_world_{};
        vec3 world_offset_{};
        vec3 position_{};
        quat orientation_{1.0F, 0.0F, 0.0F, 0.0F};
        bool valid_{};
    };

} // namespace vxr
