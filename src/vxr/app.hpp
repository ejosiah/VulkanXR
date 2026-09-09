#pragma once

#include <vulkanizer/vulkan_app.hpp>

#include <memory>

namespace vxr {

    using window_ptr = std::unique_ptr<GLFWwindow, vkz::glfw_window_deleter>;
    using surface_provider_ptr = std::unique_ptr<vkz::glfw_surface_provider>;

    class app {
      public:
        virtual ~app() = default;

        virtual void run() = 0;

        virtual void shutdown() noexcept = 0;
    };

    enum class application_mode {
        vr,
        desktop
    };

} // namespace vxr
