#pragma once

#include <vxr/input.hpp>
#include <vxr/swapchain.hpp>
#include <vxr/transforms.hpp>

#include <compare>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <variant>

namespace vxr::session {

    struct reference_space_specification {
        std::string name_;
        XrReferenceSpaceType type{XR_REFERENCE_SPACE_TYPE_VIEW};
        pose pose_in_reference_space;

        reference_space_specification &name(std::string value) {
            name_ = std::move(value);
            return *this;
        }

        reference_space_specification &view() {
            type = XR_REFERENCE_SPACE_TYPE_VIEW;
            return *this;
        }

        reference_space_specification &local() {
            type = XR_REFERENCE_SPACE_TYPE_LOCAL;
            return *this;
        }

        reference_space_specification &local_floor() {
            type = XR_REFERENCE_SPACE_TYPE_LOCAL_FLOOR_EXT;
            return *this;
        }

        reference_space_specification &stage() {
            type = XR_REFERENCE_SPACE_TYPE_STAGE;
            return *this;
        }

        reference_space_specification &translate(float x, float y, float z) {
            pose_in_reference_space.position = {x, y, z};
            return *this;
        }

        reference_space_specification &orientation(float w, float x, float y, float z) {
            pose_in_reference_space.orientation = {w, x, y, z};
            return *this;
        }

        reference_space_specification &rotate_x(float angle) {
            pose_in_reference_space.orientation = angleAxis(radians(angle), vec3{1.0F, 0.0F, 0.0F});
            return *this;
        }

        reference_space_specification &rotate_y(float angle) {
            pose_in_reference_space.orientation = angleAxis(radians(angle), vec3{0.0F, 1.0F, 0.0F});
            return *this;
        }

        reference_space_specification &rotate_z(float angle) {
            pose_in_reference_space.orientation = angleAxis(radians(angle), vec3{0.0F, 0.0F, 1.0F});
            return *this;
        }
    };

    struct action_specification {
        std::string name;
        std::string description;
        std::variant<input::binding, output::binding> binding;

        [[nodiscard]] auto operator<=>(const action_specification &other) const noexcept {
            return name <=> other.name;
        }
    };

    struct action_set_specification {
        std::string name_;
        std::string description_;
        std::set<action_specification> actions;

        action_set_specification &name(std::string_view value) {
            name_ = value;
            return *this;
        }

        action_set_specification &description(std::string_view value) {
            description_ = value;
            return *this;
        }

        action_set_specification &add_action(std::string_view action_name, input::button button, std::string_view action_description = {}) {
            actions.insert({std::string{action_name}, action_description.empty() ? std::string{action_name} : std::string{action_description}, button});
            return *this;
        }

        action_set_specification &add_action(std::string_view action_name, output::button button, std::string_view action_description = {}) {
            actions.insert({std::string{action_name}, action_description.empty() ? std::string{action_name} : std::string{action_description}, button});
            return *this;
        }

        void validate() const {
            if (name_.empty()) {
                throw std::invalid_argument{"Action-set name is required"};
            }
            for (const auto &action : actions) {
                if (action.name.empty()) {
                    throw std::invalid_argument{"Action name is required"};
                }
            }
        }
    };

    struct config {
        std::vector<swapchain_create_info> swapchains;
        std::vector<reference_space_specification> spaces;
        std::vector<action_set_specification> action_sets;
        XrReferenceSpaceType base_space_type{XR_REFERENCE_SPACE_TYPE_VIEW};

        config &add_swapchain(swapchain_create_info specification) {
            swapchains.push_back(std::move(specification));
            return *this;
        }

        config &add_space(reference_space_specification specification) {
            spaces.push_back(std::move(specification));
            return *this;
        }

        config &add_action_set(action_set_specification specification) {
            action_sets.push_back(std::move(specification));
            return *this;
        }

        config &view() {
            base_space_type = XR_REFERENCE_SPACE_TYPE_VIEW;
            return *this;
        }

        config &local() {
            base_space_type = XR_REFERENCE_SPACE_TYPE_LOCAL;
            return *this;
        }

        config &local_floor() {
            base_space_type = XR_REFERENCE_SPACE_TYPE_LOCAL_FLOOR_EXT;
            return *this;
        }

        config &stage() {
            base_space_type = XR_REFERENCE_SPACE_TYPE_STAGE;
            return *this;
        }

        config &base_space(XrReferenceSpaceType type) {
            base_space_type = type;
            return *this;
        }

        void validate() const {
            if (swapchains.empty()) {
                throw std::invalid_argument{"At least one swapchain is required"};
            }
            for (const auto &action_set : action_sets) {
                action_set.validate();
            }
        }
    };

} // namespace vxr::session
