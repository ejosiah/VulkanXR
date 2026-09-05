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
        aim,
        haptic
    };
    enum class component {
        click,
        touch,
        force,
        value,
        x,
        y,
        twist,
        pose,
        vibrate
    };

    struct binding {
        location location;
        identifier identifier;
        component component;
    };

    class simple_interaction_profile;
    class oculus_touch_controller_profile;
    class htc_vive_controller_profile;

    class interaction_profile {
      public:
        virtual ~interaction_profile() = default;

        [[nodiscard]] virtual std::string profile() const = 0;

        [[nodiscard]] virtual std::string path(identifier value) const = 0;

        [[nodiscard]] virtual std::optional<std::string> path(const binding &value) const;

        [[nodiscard]] static std::string path(location value);

        [[nodiscard]] static std::string path(component value);

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
