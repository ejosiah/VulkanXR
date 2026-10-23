#include <vxr/application_builder.hpp>

#include <stdexcept>
#include <utility>

namespace vxr {

    mirror_builder &mirror_builder::extent(std::uint32_t width, std::uint32_t height) {
        application_.mirror_config_.width = width;
        application_.mirror_config_.height = height;
        return *this;
    }

    mirror_builder &mirror_builder::multiview(bool enabled) {
        application_.mirror_config_.multiview = enabled;
        return *this;
    }

    mirror_builder &mirror_builder::vsync(bool enabled) {
        application_.mirror_config_.vsync = enabled;
        return *this;
    }

    mirror_builder &mirror_builder::eye_separation(float separation) {
        application_.mirror_config_.eye_separation = separation;
        return *this;
    }

    mirror_builder &mirror_builder::barrel_distortion(float distortion) {
        application_.mirror_config_.barrel_distortion = distortion;
        return *this;
    }

    application_builder::application_builder() : mirror_builder_{*this} {
    }

    mirror_builder &application_builder::mirror(bool enabled) {
        mirror_config_.enabled = enabled;
        return mirror_builder_;
    }

    std::unique_ptr<app> application_builder::build() {
        if (!session_config_) {
            throw std::logic_error{"An application session configuration is required"};
        }
        if (!renderer_factory_) {
            throw std::logic_error{"An application renderer factory is required"};
        }
        mirror_config_.title = context_builder_.app_name();
        if (mode_ == application_mode::desktop) {
            return std::make_unique<desktop_application>(std::move(*session_config_), renderer_factory_, std::move(mirror_config_), std::move(static_cast<vkz::builder &>(context_builder_)));
        }
        return std::make_unique<vxr::application>(std::move(context_builder_), std::move(*session_config_), renderer_factory_, std::move(mirror_config_));
    }

} // namespace vxr
