#pragma once

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

} // namespace vxr::input
