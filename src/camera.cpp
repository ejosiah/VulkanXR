#include <vxr/camera.hpp>

namespace vxr {

    void camera::update(const std::vector<XrView> &views) {
        if (views.empty()) {
            return;
        }

        tracked_head_in_world_ = compose(world_from_tracking_, head_pose(views));
        position_ = tracked_head_in_world_.position + world_offset_;
        orientation_ = tracked_head_in_world_.orientation;
        valid_ = true;
    }

    void camera::reference_space_changed(const XrEventDataReferenceSpaceChangePending &event, const std::vector<XrView> &views) {
        if (event.referenceSpaceType != XR_REFERENCE_SPACE_TYPE_STAGE || !valid_ || views.empty()) {
            return;
        }

        world_from_tracking_ = compose(tracked_head_in_world_, inverse(head_pose(views)));
    }

    void camera::move(const vec2 &direction, float distance) {
        vec3 forward = orientation_ * vec3{0.0F, 0.0F, -1.0F};
        vec3 right = orientation_ * vec3{1.0F, 0.0F, 0.0F};
        forward.y = 0.0F;
        right.y = 0.0F;
        if (dot(forward, forward) > 0.0F) {
            forward = normalize(forward);
        }
        if (dot(right, right) > 0.0F) {
            right = normalize(right);
        }

        const vec3 movement = (forward * direction.y + right * direction.x) * distance;
        world_offset_ += movement;
        position_ += movement;
    }

    void camera::turn(float angle) {
        if (!valid_) {
            return;
        }

        const auto rotation = angleAxis(angle, vec3{0.0F, 1.0F, 0.0F});
        pose turn;
        turn.orientation = rotation;
        turn.position = position_ - rotation * position_;
        world_from_tracking_ = compose(turn, world_from_tracking_);
        tracked_head_in_world_ = compose(turn, tracked_head_in_world_);
        world_offset_ = rotation * world_offset_;
        orientation_ = tracked_head_in_world_.orientation;
    }

    void camera::teleport(const vec3 &target) {
        world_offset_.x += target.x - position_.x;
        world_offset_.z += target.z - position_.z;
        position_.x = target.x;
        position_.z = target.z;
    }

    pose camera::world_pose(const pose &tracking_pose) const noexcept {
        auto result = compose(world_from_tracking_, tracking_pose);
        result.position += world_offset_;
        return result;
    }

    pose camera::world_pose(const XrPosef &tracking_pose) const noexcept {
        return world_pose(to_pose(tracking_pose));
    }

    const vec3 &camera::position() const noexcept {
        return position_;
    }

    const quat &camera::orientation() const noexcept {
        return orientation_;
    }

    bool camera::valid() const noexcept {
        return valid_;
    }

    pose camera::head_pose(const std::vector<XrView> &views) {
        auto result = to_pose(views.front().pose);
        result.position = {};
        for (const auto &view : views) {
            result.position += to_pose(view.pose).position / static_cast<float>(views.size());
        }
        return result;
    }

} // namespace vxr
