#include <vxr/application_builder.hpp>

#include <stdexcept>
#include <utility>

namespace vxr {

    application_builder &application_builder::app_name(const std::string &name) {
        context::builder::app_name(name);
        return *this;
    }

    application_builder &application_builder::vulkan_api_version(std::uint32_t version) {
        context::builder::vulkan_api_version(version);
        return *this;
    }

    application_builder &application_builder::openxr_api_version(XrVersion version) {
        context::builder::openxr_api_version(version);
        return *this;
    }

    application_builder &application_builder::form_factor(XrFormFactor form_factor) {
        context::builder::form_factor(form_factor);
        return *this;
    }

    application_builder &application_builder::add_openxr_extension(const std::string &extension) {
        context::builder::add_openxr_extension(extension);
        return *this;
    }

    application_builder &application_builder::add_optional_openxr_extension(const std::string &extension) {
        context::builder::add_optional_openxr_extension(extension);
        return *this;
    }

    application_builder &application_builder::add_openxr_layer(const std::string &layer) {
        context::builder::add_openxr_layer(layer);
        return *this;
    }

    application_builder &application_builder::session(const session::config &session_config) {
        session_config_ = &session_config;
        return *this;
    }

    application_builder &application_builder::renderer(renderer_factory renderer_factory) {
        renderer_factory_ = std::move(renderer_factory);
        return *this;
    }

    application_builder &application_builder::mode(application_mode mode) {
        mode_ = mode;
        return *this;
    }

    std::unique_ptr<app> application_builder::build() {
        if (!session_config_) {
            throw std::logic_error{"An application session configuration is required"};
        }
        if (!renderer_factory_) {
            throw std::logic_error{"An application renderer factory is required"};
        }
        mirror_config mirror;
        mirror.title = context::builder::app_name();
        if (mode_ == application_mode::desktop) {
            return std::make_unique<desktop_application>(*session_config_, renderer_factory_, std::move(mirror));
        }
        return std::make_unique<application>(*this, *session_config_, renderer_factory_, std::move(mirror));
    }

} // namespace vxr
