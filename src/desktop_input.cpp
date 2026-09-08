#include <vxr/desktop_input.hpp>

#include <utility>
#include <type_traits>

namespace vxr {

    namespace {

        [[nodiscard]] bool changed(const action_state &left, const action_state &right) {
            if (left.index() != right.index()) {
                return true;
            }
            return std::visit(
                [](const auto &left_value, const auto &right_value) {
                    using value_type = std::decay_t<decltype(left_value)>;
                    if constexpr (!std::is_same_v<value_type, std::decay_t<decltype(right_value)>>) {
                        return true;
                    } else if constexpr (std::is_same_v<value_type, bool> || std::is_same_v<value_type, float>) {
                        return left_value != right_value;
                    } else if constexpr (std::is_same_v<value_type, vec2>) {
                        return left_value.x != right_value.x || left_value.y != right_value.y;
                    } else {
                        return left_value.position.x != right_value.position.x || left_value.position.y != right_value.position.y || left_value.position.z != right_value.position.z ||
                               left_value.orientation.w != right_value.orientation.w || left_value.orientation.x != right_value.orientation.x ||
                               left_value.orientation.y != right_value.orientation.y || left_value.orientation.z != right_value.orientation.z;
                    }
                },
                left, right);
        }

    } // namespace

    desktop_input_adaptor::desktop_input_adaptor(GLFWwindow *window, const session::config &configuration) : window_{window} {
        for (const auto &set_specification : configuration.action_sets) {
            action_set set;
            set.name = set_specification.name_;
            for (const auto &specification : set_specification.actions) {
                if (const auto *binding = std::get_if<input::binding>(&specification.binding)) {
                    action value;
                    value.name = specification.name;
                    value.state = state(*binding);
                    set.actions.emplace(value.name, std::move(value));
                    bindings_.emplace(set.name + "/" + specification.name, *binding);
                }
            }
            action_sets_.emplace(set.name, std::move(set));
        }
    }

    const std::map<std::string, action_set> &desktop_input_adaptor::sync() {
        for (auto &[set_name, set] : action_sets_) {
            for (auto &[action_name, value] : set.actions) {
                const auto &binding = bindings_.at(set_name + "/" + action_name);
                const auto previous = value.state;
                value.state = state(binding);
                value.changed = changed(previous, value.state);
                value.active = true;
            }
        }
        return action_sets_;
    }

    action_state desktop_input_adaptor::state(const input::button &button) const {
        if (button.component == input::component::pose) {
            pose value;
            value.position = button.location == input::location::left_hand ? vec3{-0.25F, 1.3F, -0.5F} : vec3{0.25F, 1.3F, -0.5F};
            if (button.identifier == input::identifier::aim) {
                int width{};
                int height{};
                double cursor_x{};
                double cursor_y{};
                glfwGetWindowSize(window_, &width, &height);
                glfwGetCursorPos(window_, &cursor_x, &cursor_y);
                const float normalized_x = width > 0 ? static_cast<float>(cursor_x / static_cast<double>(width)) * 2.0F - 1.0F : 0.0F;
                const float normalized_y = height > 0 ? static_cast<float>(cursor_y / static_cast<double>(height)) * 2.0F - 1.0F : 0.0F;
                const auto yaw = angleAxis(radians(-normalized_x * 60.0F), vec3{0.0F, 1.0F, 0.0F});
                const auto pitch = angleAxis(radians(-normalized_y * 45.0F), vec3{1.0F, 0.0F, 0.0F});
                value.orientation = yaw * pitch;
            }
            return value;
        }
        if (button.component == input::component::click || button.component == input::component::touch) {
            return pressed(button);
        }
        return axis(button);
    }

    bool desktop_input_adaptor::pressed(const input::button &button) const {
        return glfwGetKey(window_, key(button)) == GLFW_PRESS;
    }

    float desktop_input_adaptor::axis(const input::button &button) const {
        const bool left = button.location == input::location::left_hand;
        if (button.component == input::component::x) {
            const int negative = left ? GLFW_KEY_A : GLFW_KEY_LEFT;
            const int positive = left ? GLFW_KEY_D : GLFW_KEY_RIGHT;
            return static_cast<float>((glfwGetKey(window_, positive) == GLFW_PRESS) - (glfwGetKey(window_, negative) == GLFW_PRESS));
        }
        if (button.component == input::component::y) {
            const int negative = left ? GLFW_KEY_S : GLFW_KEY_DOWN;
            const int positive = left ? GLFW_KEY_W : GLFW_KEY_UP;
            return static_cast<float>((glfwGetKey(window_, positive) == GLFW_PRESS) - (glfwGetKey(window_, negative) == GLFW_PRESS));
        }
        return pressed(button) ? 1.0F : 0.0F;
    }

    int desktop_input_adaptor::key(const input::button &button) {
        switch (button.identifier) {
        case input::identifier::a:
            return GLFW_KEY_J;
        case input::identifier::b:
            return GLFW_KEY_K;
        case input::identifier::x:
            return GLFW_KEY_U;
        case input::identifier::y:
            return GLFW_KEY_I;
        case input::identifier::menu:
            return GLFW_KEY_ESCAPE;
        case input::identifier::select:
            return GLFW_KEY_ENTER;
        case input::identifier::trigger:
            return button.location == input::location::left_hand ? GLFW_KEY_F : GLFW_KEY_SLASH;
        case input::identifier::squeeze:
            return button.location == input::location::left_hand ? GLFW_KEY_LEFT_SHIFT : GLFW_KEY_RIGHT_SHIFT;
        case input::identifier::thumb_stick:
        case input::identifier::track_pad:
            return button.location == input::location::left_hand ? GLFW_KEY_LEFT_CONTROL : GLFW_KEY_RIGHT_CONTROL;
        default:
            return GLFW_KEY_SPACE;
        }
    }

} // namespace vxr
