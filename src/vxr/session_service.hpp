#pragma once

#include <vxr/context.hpp>
#include <vxr/renderer.hpp>
#include <vxr/session_config.hpp>
#include <vxr/session_context.hpp>

#include <openxr/openxr.h>

#include <map>
#include <vector>

namespace vxr::session {

    class service;

    struct action_data {
        input::binding input;
        XrAction handle{XR_NULL_HANDLE};
    };

    struct action_set_binding {
        XrActionSet handle{XR_NULL_HANDLE};
        action_set *state{};
        std::map<std::string, action_data> actions;
    };

    class state {
      public:
        explicit state(service &session_service) : service_{session_service} {
        }

        virtual void enter() {
        }

        virtual void begin_frame() {
        }

        virtual void process_frame() {
        }

        virtual void end_frame() {
        }

      protected:
        service &service_;
    };

    class running_state : public state {
      public:
        using state::state;

        void begin_frame() override;

        void process_frame() override;

        void end_frame() override;

      protected:
        virtual void process_actions(XrTime display_time);

        virtual void render_frame(const frame_info &frame, layers &composition_layers);

      private:
        [[nodiscard]] view_info locate_views(XrSpace base_space, XrTime display_time) const;

        void update_space_locations(XrSpace base_space, XrTime display_time) const;
    };

    class idle_state final : public state {
      public:
        using state::state;

        void process_frame() override;
    };

    class ready_state final : public running_state {
      public:
        using running_state::running_state;

        void enter() override;
    };

    class synchronized_state final : public running_state {
      public:
        using running_state::running_state;
    };

    class visible_state final : public running_state {
      public:
        using running_state::running_state;

      protected:
        void render_frame(const frame_info &frame, layers &composition_layers) override;
    };

    class focused_state final : public running_state {
      public:
        using running_state::running_state;

      protected:
        void process_actions(XrTime display_time) override;
    };

    class stopping_state final : public state {
      public:
        using state::state;

        void enter() override;
    };

    class exiting_state : public state {
      public:
        using state::state;
    };

    class loss_pending_state final : public exiting_state {
      public:
        using exiting_state::exiting_state;
    };

    class service {
      public:
        service(vxr::context &xr_context, context &session_context, const config &configuration, renderer &renderer)
            : xr_context_{xr_context}, session_context_{session_context}, configuration_{configuration}, renderer_{renderer}, unknown_state_{*this}, idle_state_{*this}, ready_state_{*this},
              synchronized_state_{*this}, visible_state_{*this}, focused_state_{*this}, stopping_state_{*this}, exiting_state_{*this}, loss_pending_state_{*this} {
            states_ = {{XR_SESSION_STATE_UNKNOWN, &unknown_state_},           {XR_SESSION_STATE_IDLE, &idle_state_},       {XR_SESSION_STATE_READY, &ready_state_},
                       {XR_SESSION_STATE_SYNCHRONIZED, &synchronized_state_}, {XR_SESSION_STATE_VISIBLE, &visible_state_}, {XR_SESSION_STATE_FOCUSED, &focused_state_},
                       {XR_SESSION_STATE_STOPPING, &stopping_state_},         {XR_SESSION_STATE_EXITING, &exiting_state_}, {XR_SESSION_STATE_LOSS_PENDING, &loss_pending_state_}};
        }

        void init();

        void destroy() noexcept;

        void handle(const XrEventDataSessionStateChanged &event);

        void handle(const XrEventDataReferenceSpaceChangePending &event);

        void process_frame();

        void terminate() noexcept {
            termination_requested_ = true;
        }

        [[nodiscard]] bool is_running() const noexcept {
            return session_context_.state != XR_SESSION_STATE_EXITING && session_context_.state != XR_SESSION_STATE_LOSS_PENDING;
        }

        [[nodiscard]] bool should_exit() const noexcept {
            return session_context_.state == XR_SESSION_STATE_EXITING;
        }

        [[nodiscard]] XrSession handle() const noexcept {
            return session_;
        }

      private:
        friend class state;
        friend class running_state;
        friend class ready_state;
        friend class visible_state;
        friend class focused_state;
        friend class stopping_state;

        void create_session();

        void create_reference_spaces();

        void create_swapchains();

        void create_base_reference_space();

        void setup_actions();

        void bind_actions(const input::interaction_profile &profile);

        vxr::context &xr_context_;
        context &session_context_;
        const config &configuration_;
        renderer &renderer_;
        XrSession session_{XR_NULL_HANDLE};
        bool termination_requested_{};
        std::vector<action_set_binding> action_set_bindings_;
        state unknown_state_;
        idle_state idle_state_;
        ready_state ready_state_;
        synchronized_state synchronized_state_;
        visible_state visible_state_;
        focused_state focused_state_;
        stopping_state stopping_state_;
        exiting_state exiting_state_;
        loss_pending_state loss_pending_state_;
        std::map<XrSessionState, state *> states_;
    };

} // namespace vxr::session
