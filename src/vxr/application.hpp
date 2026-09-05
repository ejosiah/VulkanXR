#pragma once

#include <vxr/backoff.hpp>
#include <vxr/context.hpp>
#include <vxr/renderer.hpp>
#include <vxr/session_config.hpp>

#include <functional>
#include <memory>

namespace vxr {

    using renderer_factory = std::function<std::unique_ptr<renderer>(context &, session::context &)>;

    class application {
      public:
        application(context::builder &context_builder, const session::config &session_config, renderer_factory renderer_factory);

        void run();

        void shutdown() noexcept;

        [[nodiscard]] static bool poll_events(const context &xr_context, XrEventDataBuffer &event);

      private:
        context::builder &context_builder_;
        const session::config &session_config_;
        renderer_factory renderer_factory_;
        backoff backoff_{"Failed to create OpenXR context"};
        bool shutdown_requested_{};
    };

} // namespace vxr
