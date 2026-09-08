#pragma once

#include <vxr/application.hpp>
#include <vxr/desktop_context.hpp>
#include <vxr/desktop_input.hpp>

namespace vxr {

    class desktop_application final : public app {
      public:
        desktop_application(const session::config &session_config, renderer_factory renderer_factory, mirror_config mirror_config = {});

        void run() override;

        void shutdown() noexcept override;

      private:
        const session::config &session_config_;
        renderer_factory renderer_factory_;
        mirror_config mirror_config_;
        vkz::glfw_runtime windowing_;
        std::unique_ptr<GLFWwindow, vkz::glfw_window_deleter> window_;
        std::unique_ptr<vkz::glfw_surface_provider> surface_provider_;
        bool shutdown_requested_{};
    };

} // namespace vxr
