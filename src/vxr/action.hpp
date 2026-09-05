#pragma once

#include <vxr/transforms.hpp>

#include <openxr/openxr.h>

#include <glm/vec2.hpp>

#include <compare>
#include <map>
#include <string>
#include <variant>

namespace vxr {

    using action_state = std::variant<bool, float, glm::vec2, pose>;

    struct action {
        std::string name;
        bool changed{};
        bool active{};
        action_state state;

        template <typename T> [[nodiscard]] T value() const {
            return std::get<T>(state);
        }

        [[nodiscard]] auto operator<=>(const action &other) const noexcept {
            return name <=> other.name;
        }
    };

    struct action_set {
        std::string name;
        std::map<std::string, action> actions;

        [[nodiscard]] auto begin() noexcept {
            return actions.begin();
        }

        [[nodiscard]] auto end() noexcept {
            return actions.end();
        }

        [[nodiscard]] auto begin() const noexcept {
            return actions.begin();
        }

        [[nodiscard]] auto end() const noexcept {
            return actions.end();
        }

        [[nodiscard]] const action &get(const std::string &action_name) const {
            return actions.at(action_name);
        }
    };

    struct vibration {
        std::string action;
        XrDuration duration{XR_MIN_HAPTIC_DURATION};
        float frequency{XR_FREQUENCY_UNSPECIFIED};
        float amplitude{0.5F};
    };

} // namespace vxr
