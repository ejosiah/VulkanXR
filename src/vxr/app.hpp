#pragma once

namespace vxr {

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
