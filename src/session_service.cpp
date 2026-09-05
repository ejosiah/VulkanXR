#include <vxr/session_service.hpp>

#include <vxr/openxr_helpers.hpp>
#include <vxr/status.hpp>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <thread>
#include <utility>

namespace vxr::session {

    void service::init() {
        configuration_.validate();
        session_context_.cube_supported = xr_context_.openxr_extension_enabled(XR_KHR_COMPOSITION_LAYER_CUBE_EXTENSION_NAME);
        session_context_.equirect_supported = xr_context_.openxr_extension_enabled(XR_KHR_COMPOSITION_LAYER_EQUIRECT_EXTENSION_NAME);
        session_context_.depth_supported = xr_context_.openxr_extension_enabled(XR_KHR_COMPOSITION_LAYER_DEPTH_EXTENSION_NAME);
        create_session();
        create_reference_spaces();
        create_swapchains();
        create_base_reference_space();
        setup_actions();
        renderer_.init();
    }

    void service::destroy() noexcept {
        renderer_.cleanup();
        for (const auto &[name, space] : session_context_.action_spaces) {
            if (space != XR_NULL_HANDLE) {
                xrDestroySpace(space);
            }
        }
        session_context_.action_spaces.clear();
        for (const auto &binding : action_set_bindings_) {
            if (binding.handle != XR_NULL_HANDLE) {
                xrDestroyActionSet(binding.handle);
            }
        }
        action_set_bindings_.clear();
        session_context_.action_sets.clear();
        session_context_.active_action_sets.clear();
        for (auto &[name, swapchain] : session_context_.swapchains) {
            swapchain.destroy();
        }
        session_context_.swapchains.clear();
        for (const auto &[name, space] : session_context_.spaces) {
            if (space != XR_NULL_HANDLE) {
                xrDestroySpace(space);
            }
        }
        session_context_.spaces.clear();
        if (session_context_.base_space != XR_NULL_HANDLE) {
            xrDestroySpace(session_context_.base_space);
        }
        session_context_.base_space = XR_NULL_HANDLE;
        if (session_ != XR_NULL_HANDLE) {
            xrDestroySession(session_);
        }
        session_ = XR_NULL_HANDLE;
        session_context_.state = XR_SESSION_STATE_UNKNOWN;
    }

    void service::handle(const XrEventDataSessionStateChanged &event) {
        session_context_.state = event.state;
        states_.at(event.state)->enter();
    }

    void service::handle(const XrEventDataReferenceSpaceChangePending &event) {
        session_context_.reference_space_change = event;
    }

    void service::process_frame() {
        states_.at(session_context_.state)->process_frame();
        if (termination_requested_) {
            session_context_.state = XR_SESSION_STATE_EXITING;
        }
    }

    void service::create_session() {
        auto graphics_binding = xr_context_.graphics_binding();
        auto create_info = make_struct<XrSessionCreateInfo>();
        create_info.next = &graphics_binding;
        create_info.systemId = xr_context_.system_id();
        check(xrCreateSession(xr_context_.instance(), &create_info, &session_), "xrCreateSession");
    }

    void service::create_reference_spaces() {
        for (const auto &specification : configuration_.spaces) {
            auto create_info = make_struct<XrReferenceSpaceCreateInfo>();
            create_info.referenceSpaceType = specification.type;
            create_info.poseInReferenceSpace = to_xr_pose(specification.pose_in_reference_space);
            XrSpace space{XR_NULL_HANDLE};
            check(xrCreateReferenceSpace(session_, &create_info, &space), "xrCreateReferenceSpace");
            session_context_.spaces.emplace(specification.name_, space);
        }
    }

    void service::create_swapchains() {
        const auto formats = enumerate<std::int64_t>(xrEnumerateSwapchainFormats, session_);
        for (const auto &specification : configuration_.swapchains) {
            if (!session_context_.depth_supported && (specification.usage_flags_ & XR_SWAPCHAIN_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0) {
                spdlog::warn("Depth swapchain '{}' will not be created because XR_KHR_composition_layer_depth is not supported", specification.name_);
                continue;
            }
            if (std::ranges::find(formats, static_cast<std::int64_t>(specification.format_)) == formats.end()) {
                throw std::runtime_error{"The requested OpenXR swapchain format is unsupported"};
            }
            session_context_.swapchains.try_emplace(specification.name_, session_, specification);
        }
    }

    void service::create_base_reference_space() {
        auto create_info = make_struct<XrReferenceSpaceCreateInfo>();
        create_info.referenceSpaceType = configuration_.base_space_type;
        create_info.poseInReferenceSpace.orientation.w = 1.0F;
        check(xrCreateReferenceSpace(session_, &create_info, &session_context_.base_space), "xrCreateReferenceSpace base space");
    }

    void service::setup_actions() {
        if (configuration_.action_sets.empty()) {
            spdlog::info("No OpenXR action sets to configure");
            return;
        }

        std::vector<XrActionSet> handles;
        handles.reserve(configuration_.action_sets.size());
        for (const auto &specification : configuration_.action_sets) {
            auto create_info = make_struct<XrActionSetCreateInfo>();
            std::strncpy(create_info.actionSetName, specification.name_.c_str(), XR_MAX_ACTION_SET_NAME_SIZE - 1);
            const auto &description = specification.description_.empty() ? specification.name_ : specification.description_;
            std::strncpy(create_info.localizedActionSetName, description.c_str(), XR_MAX_LOCALIZED_ACTION_SET_NAME_SIZE - 1);

            XrActionSet handle{XR_NULL_HANDLE};
            check(xr_context_.instance(), xrCreateActionSet(xr_context_.instance(), &create_info, &handle), "xrCreateActionSet");
            handles.push_back(handle);

            auto &[name, state] = *session_context_.action_sets.try_emplace(specification.name_, action_set{specification.name_}).first;
            action_set_bindings_.push_back({handle, &state, {}});
            auto &binding = action_set_bindings_.back();

            for (const auto &action_specification : specification.actions) {
                auto action_info = make_struct<XrActionCreateInfo>();
                std::strncpy(action_info.actionName, action_specification.name.c_str(), XR_MAX_ACTION_NAME_SIZE - 1);
                std::strncpy(action_info.localizedActionName, action_specification.description.c_str(), XR_MAX_LOCALIZED_ACTION_NAME_SIZE - 1);
                action_info.actionType = input::action_type(action_specification.input.component);

                XrAction action_handle{XR_NULL_HANDLE};
                check(xr_context_.instance(), xrCreateAction(handle, &action_info, &action_handle), "xrCreateAction");
                binding.actions.emplace(action_specification.name, action_data{action_specification.input, action_handle});
                state.actions.emplace(action_specification.name, action{action_specification.name});

                if (action_info.actionType == XR_ACTION_TYPE_POSE_INPUT) {
                    auto space_info = make_struct<XrActionSpaceCreateInfo>();
                    space_info.action = action_handle;
                    space_info.poseInActionSpace.orientation.w = 1.0F;
                    XrSpace space{XR_NULL_HANDLE};
                    check(xr_context_.instance(), xrCreateActionSpace(session_, &space_info, &space), "xrCreateActionSpace");
                    session_context_.action_spaces.emplace(action_specification.name, space);
                }
            }
        }

        if (renderer_.active_action_sets() == "*") {
            for (const XrActionSet handle : handles) {
                session_context_.active_action_sets.push_back({handle, XR_NULL_PATH});
            }
        }

        bind_actions(input::interaction_profile::oculus_touch_controller());

        auto attach_info = make_struct<XrSessionActionSetsAttachInfo>();
        attach_info.countActionSets = static_cast<std::uint32_t>(handles.size());
        attach_info.actionSets = handles.data();
        check(xr_context_.instance(), xrAttachSessionActionSets(session_, &attach_info), "xrAttachSessionActionSets");
    }

    void service::bind_actions(const input::interaction_profile &profile) {
        std::vector<XrActionSuggestedBinding> suggested_bindings;
        for (const auto &action_set_binding : action_set_bindings_) {
            for (const auto &[name, action] : action_set_binding.actions) {
                const auto path = profile.path(action.input);
                if (!path) continue;
                XrPath binding_path{XR_NULL_PATH};
                check(xr_context_.instance(), xrStringToPath(xr_context_.instance(), path->c_str(), &binding_path), "xrStringToPath action binding");
                suggested_bindings.push_back({action.handle, binding_path});
            }
        }

        XrPath profile_path{XR_NULL_PATH};
        const std::string profile_name = profile.profile();
        check(xr_context_.instance(), xrStringToPath(xr_context_.instance(), profile_name.c_str(), &profile_path), "xrStringToPath interaction profile");
        auto suggested_binding_info = make_struct<XrInteractionProfileSuggestedBinding>();
        suggested_binding_info.interactionProfile = profile_path;
        suggested_binding_info.countSuggestedBindings = static_cast<std::uint32_t>(suggested_bindings.size());
        suggested_binding_info.suggestedBindings = suggested_bindings.data();
        check(xr_context_.instance(), xrSuggestInteractionProfileBindings(xr_context_.instance(), &suggested_binding_info), "xrSuggestInteractionProfileBindings");
    }

    void running_state::process_frame() {
        auto frame_state = make_struct<XrFrameState>();
        auto wait_info = make_struct<XrFrameWaitInfo>();
        check(xrWaitFrame(service_.session_, &wait_info, &frame_state), "xrWaitFrame");
        auto begin_info = make_struct<XrFrameBeginInfo>();
        check(xrBeginFrame(service_.session_, &begin_info), "xrBeginFrame");
        layers composition_layers;
        if (frame_state.shouldRender == XR_TRUE) {
            begin_frame();
            update_space_locations(service_.session_context_.base_space, frame_state.predictedDisplayTime);
            service_.session_context_.views = locate_views(service_.session_context_.base_space, frame_state.predictedDisplayTime).views;
            view_info views{make_struct<XrViewState>(), service_.session_context_.views};

            for (auto &[name, swapchain] : service_.session_context_.swapchains) {
                if (!swapchain.create_info().render_) continue;
                auto acquire_info = make_struct<XrSwapchainImageAcquireInfo>();
                std::uint32_t image_index{};
                check(xrAcquireSwapchainImage(swapchain.handle(), &acquire_info, &image_index), "xrAcquireSwapchainImage");
                auto image_wait_info = make_struct<XrSwapchainImageWaitInfo>();
                image_wait_info.timeout = XR_INFINITE_DURATION;
                check(xrWaitSwapchainImage(swapchain.handle(), &image_wait_info), "xrWaitSwapchainImage");

                std::optional<image_id> depth_image;
                vxr::swapchain *depth_swapchain{};
                if (service_.session_context_.depth_supported && !swapchain.create_info().depth_.empty()) {
                    depth_swapchain = &service_.session_context_.swapchains.at(swapchain.create_info().depth_);
                    std::uint32_t depth_image_index{};
                    check(xrAcquireSwapchainImage(depth_swapchain->handle(), &acquire_info, &depth_image_index), "xrAcquireSwapchainImage depth");
                    check(xrWaitSwapchainImage(depth_swapchain->handle(), &image_wait_info), "xrWaitSwapchainImage depth");
                    depth_image = image_id{depth_swapchain->handle(), depth_image_index};
                }

                render_frame({{swapchain.handle(), image_index}, views, service_.session_context_.base_space, frame_state.predictedDisplayTime, frame_state.predictedDisplayPeriod, depth_image},
                             composition_layers);

                auto release_info = make_struct<XrSwapchainImageReleaseInfo>();
                if (depth_swapchain) {
                    check(xrReleaseSwapchainImage(depth_swapchain->handle(), &release_info), "xrReleaseSwapchainImage depth");
                }
                check(xrReleaseSwapchainImage(swapchain.handle(), &release_info), "xrReleaseSwapchainImage");
            }
            end_frame();
        }

        std::ranges::sort(composition_layers, {}, &layer::position);
        std::vector<XrCompositionLayerBaseHeader *> headers;
        headers.reserve(composition_layers.size());
        for (const auto &entry : composition_layers) {
            std::visit(
                [&](auto *value) {
                    value->space = service_.session_context_.base_space;
                    headers.push_back(reinterpret_cast<XrCompositionLayerBaseHeader *>(value));
                },
                entry.value);
        }

        auto end_info = make_struct<XrFrameEndInfo>();
        end_info.displayTime = frame_state.predictedDisplayTime;
        end_info.environmentBlendMode = service_.session_context_.blend_mode;
        end_info.layerCount = static_cast<std::uint32_t>(headers.size());
        end_info.layers = headers.data();
        check(xrEndFrame(service_.session_, &end_info), "xrEndFrame");
    }

    void running_state::begin_frame() {
        service_.renderer_.begin_frame();
    }

    void running_state::end_frame() {
        service_.renderer_.end_frame();
    }

    void running_state::render_frame(const frame_info &frame, layers &composition_layers) {
        service_.renderer_.render(frame, composition_layers);
    }

    view_info running_state::locate_views(XrSpace base_space, XrTime display_time) const {
        auto state = make_struct<XrViewState>();
        auto locate_info = make_struct<XrViewLocateInfo>();
        locate_info.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        locate_info.displayTime = display_time;
        locate_info.space = base_space;
        return {state, vxr::locate_views(service_.session_, locate_info, state)};
    }

    void running_state::update_space_locations(XrSpace base_space, XrTime display_time) const {
        std::vector<space_location> locations;
        for (const auto &[name, space] : service_.session_context_.spaces) {
            const auto location = locate_space(space, base_space, display_time);
            const XrSpaceLocationFlags required = XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_POSITION_VALID_BIT;
            if ((location.locationFlags & required) == required) {
                locations.push_back({name, to_pose(location.pose)});
            }
        }
        if (!locations.empty()) {
            service_.renderer_.set(locations);
        }
    }

    void idle_state::process_frame() {
        using namespace std::chrono_literals;
        std::this_thread::sleep_for(10ms);
    }

    void ready_state::enter() {
        const auto configurations = enumerate<XrViewConfigurationType>(xrEnumerateViewConfigurations, service_.xr_context_.instance(), service_.xr_context_.system_id());
        if (std::ranges::find(configurations, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO) == configurations.end()) {
            throw std::runtime_error{"The OpenXR runtime does not support primary stereo views"};
        }
        auto begin_info = make_struct<XrSessionBeginInfo>();
        begin_info.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        check(xrBeginSession(service_.session_, &begin_info), "xrBeginSession");
    }

    void visible_state::render_frame(const frame_info &frame, layers &composition_layers) {
        service_.renderer_.paused(frame, composition_layers);
    }

    void stopping_state::enter() {
        check(xrEndSession(service_.session_), "xrEndSession");
    }

} // namespace vxr::session
