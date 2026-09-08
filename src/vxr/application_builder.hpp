#pragma once

#include <vxr/app.hpp>
#include <vxr/application.hpp>
#include <vxr/desktop_application.hpp>

#include <memory>

namespace vxr {

    class application_builder : public context::builder {
      public:
        application_builder() = default;

        application_builder &app_name(const std::string &name);

        application_builder &vulkan_api_version(std::uint32_t version);

        application_builder &openxr_api_version(XrVersion version);

        application_builder &form_factor(XrFormFactor form_factor);

        application_builder &add_openxr_extension(const std::string &extension);

        application_builder &add_optional_openxr_extension(const std::string &extension);

        application_builder &add_openxr_layer(const std::string &layer);

        application_builder &session(const session::config &session_config);

        application_builder &renderer(renderer_factory renderer_factory);

        application_builder &mode(application_mode mode);

        [[nodiscard]] std::unique_ptr<app> build();

      private:
        const session::config *session_config_{};
        renderer_factory renderer_factory_;
        application_mode mode_{application_mode::vr};
    };

} // namespace vxr
