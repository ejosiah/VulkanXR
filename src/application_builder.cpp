#include <vxr/application_builder.hpp>

#include <stdexcept>
#include <utility>

namespace vxr {

    application_builder &application_builder::app_name(const std::string &name) {
        context_builder_.app_name(name);
        return *this;
    }

    application_builder &application_builder::app_version(const std::string &version) {
        context_builder_.app_version(version);
        return *this;
    }

    application_builder &application_builder::engine_name(const std::string &name) {
        context_builder_.engine_name(name);
        return *this;
    }

    application_builder &application_builder::engine_version(const std::string &version) {
        context_builder_.engine_version(version);
        return *this;
    }

    application_builder &application_builder::vulkan_api_version(std::uint32_t version) {
        context_builder_.vulkan_api_version(version);
        return *this;
    }

    application_builder &application_builder::openxr_api_version(XrVersion version) {
        context_builder_.openxr_api_version(version);
        return *this;
    }

    application_builder &application_builder::form_factor(XrFormFactor form_factor) {
        context_builder_.form_factor(form_factor);
        return *this;
    }

    application_builder &application_builder::add_openxr_extension(const std::string &extension) {
        context_builder_.add_openxr_extension(extension);
        return *this;
    }

    application_builder &application_builder::add_optional_openxr_extension(const std::string &extension) {
        context_builder_.add_optional_openxr_extension(extension);
        return *this;
    }

    application_builder &application_builder::add_openxr_layer(const std::string &layer) {
        context_builder_.add_openxr_layer(layer);
        return *this;
    }

    application_builder &application_builder::add_instance_extension(const std::string &extension) {
        context_builder_.add_instance_extension(extension);
        return *this;
    }

    application_builder &application_builder::add_instance_layer(const std::string &layer) {
        context_builder_.add_instance_layer(layer);
        return *this;
    }

    application_builder &application_builder::add_device_extension(const std::string &extension) {
        context_builder_.add_device_extension(extension);
        return *this;
    }

    application_builder &application_builder::add_device_layer(const std::string &layer) {
        context_builder_.add_device_layer(layer);
        return *this;
    }

    application_builder &application_builder::enabled_features(const VkPhysicalDeviceFeatures &features) {
        context_builder_.enabled_features(features);
        return *this;
    }

    application_builder &application_builder::surface(const vkz::surface_provider &surface_provider) {
        context_builder_.surface(surface_provider);
        return *this;
    }

    application_builder &application_builder::add_queue(VkQueueFlagBits flags) {
        context_builder_.add_queue(flags);
        return *this;
    }

    application_builder &application_builder::add_unique_queue(VkQueueFlagBits flags) {
        context_builder_.add_unique_queue(flags);
        return *this;
    }

    application_builder &application_builder::num_graphics_queues(std::uint32_t count) {
        context_builder_.num_graphics_queues(count);
        return *this;
    }

    application_builder &application_builder::add_extension_chain(const vkz::device_extension_chain &extensions) {
        context_builder_.add_extension_chain(extensions);
        return *this;
    }

    application_builder &application_builder::session(session::config session_config) {
        session_config_ = std::move(session_config);
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
        mirror.title = context_builder_.app_name();
        if (mode_ == application_mode::desktop) {
            return std::make_unique<desktop_application>(std::move(*session_config_), renderer_factory_, std::move(mirror));
        }
        return std::make_unique<application>(context_builder_, std::move(*session_config_), renderer_factory_, std::move(mirror));
    }

} // namespace vxr
