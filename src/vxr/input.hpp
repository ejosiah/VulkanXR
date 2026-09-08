#pragma once

#include <openxr/openxr.h>

#include <optional>
#include <stdexcept>
#include <string>

namespace vxr::input {

    enum class location {
        left_hand,
        right_hand,
        head,
        game_pad,
        treadmill
    };
    enum class identifier {
        menu,
        select,
        track_pad,
        thumb_stick,
        joy_stick,
        trigger,
        throttle,
        trackball,
        pedal,
        system,
        d_pad_up,
        d_pad_down,
        d_pad_left,
        d_pad_right,
        a,
        b,
        x,
        y,
        volume_up,
        volume_down,
        thumb_rest,
        shoulder,
        squeeze,
        wheel,
        grip,
        aim
    };
    enum class component {
        click,
        touch,
        force,
        value,
        x,
        y,
        twist,
        pose
    };

    struct button {
        location location;
        identifier identifier;
        component component;
    };

    using binding = button;

    namespace simple {

        struct layout {
            static constexpr button left_select_click{location::left_hand, identifier::select, component::click};
            static constexpr button right_select_click{location::right_hand, identifier::select, component::click};
            static constexpr button left_menu_click{location::left_hand, identifier::menu, component::click};
            static constexpr button right_menu_click{location::right_hand, identifier::menu, component::click};
            static constexpr button left_grip_pose{location::left_hand, identifier::grip, component::pose};
            static constexpr button right_grip_pose{location::right_hand, identifier::grip, component::pose};
            static constexpr button left_aim_pose{location::left_hand, identifier::aim, component::pose};
            static constexpr button right_aim_pose{location::right_hand, identifier::aim, component::pose};
        };

    } // namespace simple

    namespace oculus_touch {

        struct layout {
            static constexpr button left_x_click{location::left_hand, identifier::x, component::click};
            static constexpr button left_y_click{location::left_hand, identifier::y, component::click};
            static constexpr button right_a_click{location::right_hand, identifier::a, component::click};
            static constexpr button right_b_click{location::right_hand, identifier::b, component::click};
            static constexpr button left_menu_click{location::left_hand, identifier::menu, component::click};
            static constexpr button left_trigger_click{location::left_hand, identifier::trigger, component::click};
            static constexpr button right_trigger_click{location::right_hand, identifier::trigger, component::click};
            static constexpr button left_squeeze_click{location::left_hand, identifier::squeeze, component::click};
            static constexpr button right_squeeze_click{location::right_hand, identifier::squeeze, component::click};
            static constexpr button left_squeeze_value{location::left_hand, identifier::squeeze, component::value};
            static constexpr button right_squeeze_value{location::right_hand, identifier::squeeze, component::value};
            static constexpr button left_thumbstick_x{location::left_hand, identifier::thumb_stick, component::x};
            static constexpr button left_thumbstick_y{location::left_hand, identifier::thumb_stick, component::y};
            static constexpr button left_thumbstick_click{location::left_hand, identifier::thumb_stick, component::click};
            static constexpr button right_thumbstick_x{location::right_hand, identifier::thumb_stick, component::x};
            static constexpr button right_thumbstick_y{location::right_hand, identifier::thumb_stick, component::y};
            static constexpr button right_thumbstick_click{location::right_hand, identifier::thumb_stick, component::click};
            static constexpr button left_grip_pose{location::left_hand, identifier::grip, component::pose};
            static constexpr button right_grip_pose{location::right_hand, identifier::grip, component::pose};
            static constexpr button left_aim_pose{location::left_hand, identifier::aim, component::pose};
            static constexpr button right_aim_pose{location::right_hand, identifier::aim, component::pose};
        };

    } // namespace oculus_touch

    namespace htc_vive {

        struct layout {
            static constexpr button left_system_click{location::left_hand, identifier::system, component::click};
            static constexpr button right_system_click{location::right_hand, identifier::system, component::click};
            static constexpr button left_menu_click{location::left_hand, identifier::menu, component::click};
            static constexpr button right_menu_click{location::right_hand, identifier::menu, component::click};
            static constexpr button left_trigger_click{location::left_hand, identifier::trigger, component::click};
            static constexpr button right_trigger_click{location::right_hand, identifier::trigger, component::click};
            static constexpr button left_squeeze_click{location::left_hand, identifier::squeeze, component::click};
            static constexpr button right_squeeze_click{location::right_hand, identifier::squeeze, component::click};
            static constexpr button left_squeeze_value{location::left_hand, identifier::squeeze, component::value};
            static constexpr button right_squeeze_value{location::right_hand, identifier::squeeze, component::value};
            static constexpr button left_trackpad_x{location::left_hand, identifier::track_pad, component::x};
            static constexpr button left_trackpad_y{location::left_hand, identifier::track_pad, component::y};
            static constexpr button left_trackpad_click{location::left_hand, identifier::track_pad, component::click};
            static constexpr button right_trackpad_x{location::right_hand, identifier::track_pad, component::x};
            static constexpr button right_trackpad_y{location::right_hand, identifier::track_pad, component::y};
            static constexpr button right_trackpad_click{location::right_hand, identifier::track_pad, component::click};
            static constexpr button left_grip_pose{location::left_hand, identifier::grip, component::pose};
            static constexpr button right_grip_pose{location::right_hand, identifier::grip, component::pose};
            static constexpr button left_aim_pose{location::left_hand, identifier::aim, component::pose};
            static constexpr button right_aim_pose{location::right_hand, identifier::aim, component::pose};
        };

    } // namespace htc_vive

} // namespace vxr::input

namespace vxr::output {

    enum class identifier {
        haptic
    };

    enum class component {
        vibrate
    };

    struct button {
        input::location location;
        identifier identifier;
        component component;
    };

    using binding = button;

    struct layout {
        static constexpr button left_haptic{input::location::left_hand, identifier::haptic, component::vibrate};
        static constexpr button right_haptic{input::location::right_hand, identifier::haptic, component::vibrate};
    };

    [[nodiscard]] XrActionType action_type(component value);

} // namespace vxr::output

namespace vxr::input {

    class simple_interaction_profile;
    class oculus_touch_controller_profile;
    class htc_vive_controller_profile;

    class interaction_profile {
      public:
        virtual ~interaction_profile() = default;

        [[nodiscard]] virtual std::string profile() const = 0;

        [[nodiscard]] virtual std::string path(identifier value) const = 0;

        [[nodiscard]] virtual std::optional<std::string> path(const binding &value) const;

        [[nodiscard]] std::optional<std::string> path(const output::binding &value) const;

        [[nodiscard]] static std::string path(location value);

        [[nodiscard]] static std::string path(component value);

        [[nodiscard]] static std::string path(output::identifier value);

        [[nodiscard]] static std::string path(output::component value);

        [[nodiscard]] static simple_interaction_profile simple();

        [[nodiscard]] static oculus_touch_controller_profile oculus_touch_controller();

        [[nodiscard]] static htc_vive_controller_profile htc_vive_controller();
    };

    class simple_interaction_profile final : public interaction_profile {
      public:
        [[nodiscard]] std::string profile() const override;

        [[nodiscard]] std::string path(identifier value) const override;
    };

    class oculus_touch_controller_profile final : public interaction_profile {
      public:
        [[nodiscard]] std::string profile() const override;

        [[nodiscard]] std::string path(identifier value) const override;

        [[nodiscard]] std::optional<std::string> path(const binding &value) const override;
    };

    class htc_vive_controller_profile final : public interaction_profile {
      public:
        [[nodiscard]] std::string profile() const override;

        [[nodiscard]] std::string path(identifier value) const override;

        [[nodiscard]] std::optional<std::string> path(const binding &value) const override;
    };

    [[nodiscard]] XrActionType action_type(component value);

} // namespace vxr::input
