#pragma once

#include <vxr/app.hpp>
#include <vxr/application.hpp>
#include <vxr/desktop_application.hpp>

#include <memory>
#include <optional>
#include <utility>

namespace vxr {

    class application_builder;

    template <typename Derived> class application_builder_dsl {
      public:
        Derived &app_name(const std::string &name) {
            application().context_builder_.app_name(name);
            return derived();
        }

        Derived &app_version(const std::string &version) {
            application().context_builder_.app_version(version);
            return derived();
        }

        Derived &engine_name(const std::string &name) {
            application().context_builder_.engine_name(name);
            return derived();
        }

        Derived &engine_version(const std::string &version) {
            application().context_builder_.engine_version(version);
            return derived();
        }

        Derived &vulkan_api_version(std::uint32_t version) {
            application().context_builder_.vulkan_api_version(version);
            return derived();
        }

        Derived &openxr_api_version(XrVersion version) {
            application().context_builder_.openxr_api_version(version);
            return derived();
        }

        Derived &form_factor(XrFormFactor form_factor) {
            application().context_builder_.form_factor(form_factor);
            return derived();
        }

        Derived &add_openxr_extension(const std::string &extension) {
            application().context_builder_.add_openxr_extension(extension);
            return derived();
        }

        Derived &add_optional_openxr_extension(const std::string &extension) {
            application().context_builder_.add_optional_openxr_extension(extension);
            return derived();
        }

        Derived &add_openxr_layer(const std::string &layer) {
            application().context_builder_.add_openxr_layer(layer);
            return derived();
        }

        Derived &add_instance_extension(const std::string &extension) {
            application().context_builder_.add_instance_extension(extension);
            return derived();
        }

        Derived &add_instance_layer(const std::string &layer) {
            application().context_builder_.add_instance_layer(layer);
            return derived();
        }

        Derived &add_device_extension(const std::string &extension) {
            application().context_builder_.add_device_extension(extension);
            return derived();
        }

        Derived &add_device_layer(const std::string &layer) {
            application().context_builder_.add_device_layer(layer);
            return derived();
        }

        Derived &enabled_features(const VkPhysicalDeviceFeatures &features) {
            application().context_builder_.enabled_features(features);
            return derived();
        }

        Derived &surface(const vkz::surface_provider &surface_provider) {
            application().context_builder_.surface(surface_provider);
            return derived();
        }

        Derived &add_queue(vkz::queue flags) {
            application().context_builder_.add_queue(flags);
            return derived();
        }

        Derived &add_unique_queue(vkz::queue flags) {
            application().context_builder_.add_unique_queue(flags);
            return derived();
        }

        Derived &num_graphics_queues(std::uint32_t count) {
            application().context_builder_.num_graphics_queues(count);
            return derived();
        }

        Derived &add_extension_chain(const vkz::device_extension_chain &extensions) {
            application().context_builder_.add_extension_chain(extensions);
            return derived();
        }

        template <vkz::vulkan_structure T> Derived &add_extension(const T &extension) {
            application().context_builder_.add_extension(extension);
            return derived();
        }

        Derived &session(session::config session_config) {
            application().session_config_ = std::move(session_config);
            return derived();
        }

        Derived &renderer(renderer_factory renderer_factory) {
            application().renderer_factory_ = std::move(renderer_factory);
            return derived();
        }

        Derived &mode(application_mode mode) {
            application().mode_ = mode;
            return derived();
        }

        [[nodiscard]] std::unique_ptr<app> build() {
            return application().build();
        }

      private:
        [[nodiscard]] Derived &derived() {
            return static_cast<Derived &>(*this);
        }

        [[nodiscard]] application_builder &application() {
            return derived().application();
        }
    };

    class mirror_builder final : public application_builder_dsl<mirror_builder> {
      public:
        explicit mirror_builder(application_builder &application)
        : application_{application} {}

        mirror_builder &extent(std::uint32_t width, std::uint32_t height);

        mirror_builder &multiview(bool enabled = true);

        mirror_builder &vsync(bool enabled = true);

        mirror_builder &eye_separation(float separation);

        mirror_builder &barrel_distortion(float distortion);

      private:
        friend class application_builder_dsl<mirror_builder>;

        [[nodiscard]] application_builder &application() {
            return application_;
        }

        application_builder &application_;
    };

    class application_builder final : public application_builder_dsl<application_builder> {
      public:
        application_builder();

        application_builder(const application_builder &) = delete;

        application_builder &operator=(const application_builder &) = delete;

        application_builder(application_builder &&) = delete;

        application_builder &operator=(application_builder &&) = delete;

        mirror_builder &mirror(bool enabled = true);

        [[nodiscard]] std::unique_ptr<app> build();

      private:
        template <typename Derived> friend class application_builder_dsl;
        friend class mirror_builder;

        [[nodiscard]] application_builder &application() {
            return *this;
        }

        context::builder context_builder_;
        std::optional<session::config> session_config_;
        renderer_factory renderer_factory_;
        application_mode mode_{application_mode::vr};
        mirror_config mirror_config_;
        mirror_builder mirror_builder_;
    };

} // namespace vxr
