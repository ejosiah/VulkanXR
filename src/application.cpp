#include <vxr/application.hpp>

#include <vxr/openxr_helpers.hpp>
#include <vxr/session_service.hpp>
#include <vxr/status.hpp>

#include <spdlog/spdlog.h>

#include <exception>
#include <utility>

namespace vxr {

    application::application(context::builder &context_builder, const session::config &session_config, renderer_factory renderer_factory)
        : context_builder_{context_builder}, session_config_{session_config}, renderer_factory_{std::move(renderer_factory)} {
    }

    void application::run() {
        while (!shutdown_requested_) {
            try {
                auto xr_context = context_builder_.build();
                backoff_.reset();

                session::context session_context;
                auto renderer = renderer_factory_(xr_context, session_context);
                if (!renderer) {
                    throw std::runtime_error{"The renderer factory returned a null renderer"};
                }

                session::service session_service{xr_context, session_context, session_config_, *renderer};
                session_service.init();

                auto event = make_struct<XrEventDataBuffer>();
                while (!shutdown_requested_ && session_service.is_running()) {
                    while (poll_events(xr_context, event)) {
                        switch (event.type) {
                        case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED:
                            session_service.handle(reinterpret_cast<const XrEventDataSessionStateChanged &>(event));
                            break;
                        case XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING:
                            session_service.handle(reinterpret_cast<const XrEventDataReferenceSpaceChangePending &>(event));
                            break;
                        case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
                            session_service.terminate();
                            break;
                        default: {
                            char event_type_name[XR_MAX_STRUCTURE_NAME_SIZE]{};
                            const XrResult result = xrStructureTypeToString(xr_context.instance(), event.type, event_type_name);
                            if (XR_SUCCEEDED(result)) {
                                spdlog::warn("OpenXR event type {} ({}) is not handled", event_type_name, static_cast<std::int32_t>(event.type));
                            } else {
                                spdlog::warn("OpenXR event type {} is not handled", static_cast<std::int32_t>(event.type));
                            }
                            break;
                        }
                        }
                    }

                    session_service.process_frame();
                }

                const bool should_exit = shutdown_requested_ || session_service.should_exit();
                session_service.destroy();
                if (should_exit) break;
            } catch (const std::exception &error) {
                spdlog::error("{}", error.what());
                backoff_();
            }
        }

        spdlog::info("OpenXR instance terminated, exiting application");
    }

    void application::shutdown() noexcept {
        shutdown_requested_ = true;
    }

    bool application::poll_events(const context &xr_context, XrEventDataBuffer &event) {
        event = make_struct<XrEventDataBuffer>();
        const XrResult result = xrPollEvent(xr_context.instance(), &event);
        if (result == XR_EVENT_UNAVAILABLE) {
            return false;
        }
        check(xr_context.instance(), result, "xrPollEvent");
        return true;
    }

} // namespace vxr
