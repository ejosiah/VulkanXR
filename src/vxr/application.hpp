#pragma once

#include <vxr/app.hpp>
#include <vxr/backoff.hpp>
#include <vxr/context.hpp>
#include <vxr/mirror.hpp>
#include <vxr/renderer.hpp>
#include <vxr/session_config.hpp>

#include <functional>
#include <memory>

namespace vxr {

    using renderer_factory = std::function<std::unique_ptr<renderer>(rendering::context &)>;

    class application final : public app {
      public:
        application(context::builder &context_builder, session::config session_config, renderer_factory renderer_factory, mirror_config mirror_config = {});

        void run() override;

        void shutdown() noexcept override;

        [[nodiscard]] static bool poll_events(const context &xr_context, XrEventDataBuffer &event);

      private:
        context::builder &context_builder_;
        session::config session_config_;
        renderer_factory renderer_factory_;
        mirror_config mirror_config_;
        vkz::glfw_runtime windowing_;
        window_ptr window_;
        surface_provider_ptr surface_provider_;
        backoff backoff_{"Failed to create OpenXR context"};
        bool shutdown_requested_{};
    };

} // namespace vxr
