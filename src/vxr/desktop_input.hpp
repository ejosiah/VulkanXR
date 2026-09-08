#pragma once

#include <vxr/action.hpp>
#include <vxr/session_config.hpp>

#include <GLFW/glfw3.h>

#include <map>
#include <string>

namespace vxr {

    class desktop_input_adaptor {
      public:
        desktop_input_adaptor(GLFWwindow *window, const session::config &configuration);

        [[nodiscard]] const std::map<std::string, action_set> &sync();

      private:
        [[nodiscard]] action_state state(const input::button &button) const;

        [[nodiscard]] bool pressed(const input::button &button) const;

        [[nodiscard]] float axis(const input::button &button) const;

        [[nodiscard]] static int key(const input::button &button);

        GLFWwindow *window_{};
        std::map<std::string, action_set> action_sets_;
        std::map<std::string, input::button> bindings_;
    };

} // namespace vxr
