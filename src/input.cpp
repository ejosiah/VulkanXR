#include <vxr/input.hpp>

namespace vxr::input {

    std::optional<std::string> interaction_profile::path(const binding &value) const {
        try {
            return path(value.location) + "/" + path(value.identifier) + path(value.component);
        } catch (...) {
            return std::nullopt;
        }
    }

    std::string interaction_profile::path(location value) {
        switch (value) {
        case location::left_hand:
            return "/user/hand/left";
        case location::right_hand:
            return "/user/hand/right";
        case location::head:
            return "/user/head";
        case location::game_pad:
            return "/user/gamepad";
        case location::treadmill:
            return "/user/treadmill";
        }
        throw std::invalid_argument{"Invalid input location"};
    }

    std::string interaction_profile::path(component value) {
        switch (value) {
        case component::vibrate:
            return "";
        case component::click:
            return "/click";
        case component::touch:
            return "/touch";
        case component::force:
            return "/force";
        case component::value:
            return "/value";
        case component::x:
            return "/x";
        case component::y:
            return "/y";
        case component::twist:
            return "/twist";
        case component::pose:
            return "/pose";
        }
        throw std::invalid_argument{"Invalid input component"};
    }

    simple_interaction_profile interaction_profile::simple() {
        return {};
    }

    oculus_touch_controller_profile interaction_profile::oculus_touch_controller() {
        return {};
    }

    htc_vive_controller_profile interaction_profile::htc_vive_controller() {
        return {};
    }

    std::string simple_interaction_profile::profile() const {
        return "/interaction_profiles/khr/simple_controller";
    }

    std::string simple_interaction_profile::path(identifier value) const {
        switch (value) {
        case identifier::select:
            return "input/select";
        case identifier::menu:
            return "input/menu";
        case identifier::grip:
            return "input/grip";
        case identifier::aim:
            return "input/aim";
        case identifier::haptic:
            return "output/haptic";
        default:
            throw std::invalid_argument{"Identifier is not supported by the simple-controller profile"};
        }
    }

    std::string oculus_touch_controller_profile::profile() const {
        return "/interaction_profiles/oculus/touch_controller";
    }

    std::optional<std::string> oculus_touch_controller_profile::path(const binding &value) const {
        binding adjusted = value;
        if (value.location == location::left_hand) {
            if (value.identifier == identifier::a) {
                adjusted.identifier = identifier::x;
            } else if (value.identifier == identifier::b) {
                adjusted.identifier = identifier::y;
            }
        } else if (value.location == location::right_hand) {
            if (value.identifier == identifier::x) {
                adjusted.identifier = identifier::a;
            } else if (value.identifier == identifier::y) {
                adjusted.identifier = identifier::b;
            } else if (value.identifier == identifier::menu) {
                return std::nullopt;
            }
        }
        return interaction_profile::path(adjusted);
    }

    std::string oculus_touch_controller_profile::path(identifier value) const {
        switch (value) {
        case identifier::x:
            return "input/x";
        case identifier::y:
            return "input/y";
        case identifier::a:
            return "input/a";
        case identifier::b:
            return "input/b";
        case identifier::menu:
            return "input/menu";
        case identifier::squeeze:
            return "input/squeeze";
        case identifier::trigger:
            return "input/trigger";
        case identifier::thumb_stick:
            return "input/thumbstick";
        case identifier::thumb_rest:
            return "input/thumbrest";
        case identifier::grip:
            return "input/grip";
        case identifier::aim:
            return "input/aim";
        case identifier::haptic:
            return "output/haptic";
        default:
            throw std::invalid_argument{"Identifier is not supported by the Oculus Touch profile"};
        }
    }

    std::string htc_vive_controller_profile::profile() const {
        return "/interaction_profiles/htc/vive_controller";
    }

    std::string htc_vive_controller_profile::path(identifier value) const {
        switch (value) {
        case identifier::system:
            return "input/system";
        case identifier::squeeze:
            return "input/squeeze";
        case identifier::menu:
            return "input/menu";
        case identifier::trigger:
            return "input/trigger";
        case identifier::track_pad:
            return "input/trackpad";
        case identifier::grip:
            return "input/grip";
        case identifier::aim:
            return "input/aim";
        case identifier::haptic:
            return "output/haptic";
        default:
            throw std::invalid_argument{"Identifier is not supported by the HTC Vive Controller profile"};
        }
    }

    std::optional<std::string> htc_vive_controller_profile::path(const binding &value) const {
        if (value.location != location::left_hand && value.location != location::right_hand) {
            return std::nullopt;
        }

        if (value.identifier == identifier::squeeze && value.component == component::value) {
            binding adjusted = value;
            adjusted.identifier = identifier::trigger;
            return interaction_profile::path(adjusted);
        }

        const bool supported =
            (value.identifier == identifier::system && value.component == component::click) ||
            (value.identifier == identifier::squeeze && value.component == component::click) ||
            (value.identifier == identifier::menu && value.component == component::click) ||
            (value.identifier == identifier::trigger && (value.component == component::click || value.component == component::value)) ||
            (value.identifier == identifier::track_pad &&
             (value.component == component::x || value.component == component::y || value.component == component::click || value.component == component::touch)) ||
            (value.identifier == identifier::grip && value.component == component::pose) || (value.identifier == identifier::aim && value.component == component::pose) ||
            (value.identifier == identifier::haptic && value.component == component::vibrate);
        if (!supported) {
            return std::nullopt;
        }
        return interaction_profile::path(value);
    }

    XrActionType action_type(component value) {
        switch (value) {
        case component::click:
        case component::touch:
            return XR_ACTION_TYPE_BOOLEAN_INPUT;
        case component::force:
        case component::value:
        case component::twist:
            return XR_ACTION_TYPE_FLOAT_INPUT;
        case component::x:
        case component::y:
            return XR_ACTION_TYPE_VECTOR2F_INPUT;
        case component::pose:
            return XR_ACTION_TYPE_POSE_INPUT;
        case component::vibrate:
            return XR_ACTION_TYPE_VIBRATION_OUTPUT;
        }
        throw std::invalid_argument{"Invalid input component"};
    }

} // namespace vxr::input
