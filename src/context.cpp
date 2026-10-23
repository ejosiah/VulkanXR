#include <vxr/context.hpp>

#include <vxr/extension_dispatch.hpp>
#include <vxr/openxr_helpers.hpp>
#include <vxr/status.hpp>

#include <volk.h>
#include <spdlog/spdlog.h>
#include <vulkanizer/log.hpp>
#include <vulkanizer/video.hpp>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <tuple>
#include <utility>

namespace vxr {

    namespace {

        XrBool32 XRAPI_PTR openxr_debug_callback(XrDebugUtilsMessageSeverityFlagsEXT severity, XrDebugUtilsMessageTypeFlagsEXT, const XrDebugUtilsMessengerCallbackDataEXT *data, void *) {
            if ((severity & XR_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0) {
                spdlog::error("{}: {}", data->functionName, data->message);
            } else if ((severity & XR_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) != 0) {
                spdlog::warn("{}: {}", data->functionName, data->message);
            } else if ((severity & XR_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) != 0) {
                spdlog::info("{}: {}", data->functionName, data->message);
            } else {
                spdlog::debug("{}: {}", data->functionName, data->message);
            }
            return XR_FALSE;
        }

        XrDebugUtilsMessengerCreateInfoEXT openxr_debug_create_info() {
            auto create_info = make_struct<XrDebugUtilsMessengerCreateInfoEXT>();
            create_info.messageSeverities = XR_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | XR_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT | XR_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                            XR_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            create_info.messageTypes = XR_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | XR_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | XR_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT |
                                       XR_DEBUG_UTILS_MESSAGE_TYPE_CONFORMANCE_BIT_EXT;
            create_info.userCallback = openxr_debug_callback;
            return create_info;
        }

        void check_vk(VkResult result, const std::string &operation) {
            if (result != VK_SUCCESS) {
                throw std::runtime_error{operation + " failed with VkResult " + std::to_string(result)};
            }
        }

        class openxr_device_factory final : public vkz::device_factory {
          public:
            openxr_device_factory(XrInstance instance, XrSystemId system_id, const extension_dispatch &extensions, std::uint32_t api_version, std::uint32_t &graphics_queue_family)
                : instance_{instance}, system_id_{system_id}, extensions_{extensions}, api_version_{api_version}, graphics_queue_family_{graphics_queue_family} {
            }

            VkInstance create_instance() override {
                check_vk(volkInitialize(), "volkInitialize");

                auto application_info = VkApplicationInfo{VK_STRUCTURE_TYPE_APPLICATION_INFO};
                application_info.pApplicationName = "VulkanXR";
                application_info.apiVersion = api_version_;

                auto instance_create_info = VkInstanceCreateInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
                instance_create_info.pApplicationInfo = &application_info;
                const auto extensions = instance_extensions();
                const auto layers = instance_layers();
                instance_create_info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
                instance_create_info.ppEnabledExtensionNames = extensions.data();
                instance_create_info.enabledLayerCount = static_cast<uint32_t>(layers.size());
                instance_create_info.ppEnabledLayerNames = layers.data();

                auto create_info = make_struct<XrVulkanInstanceCreateInfoKHR>();
                create_info.systemId = system_id_;
                create_info.pfnGetInstanceProcAddr = vkGetInstanceProcAddr;
                create_info.vulkanCreateInfo = &instance_create_info;

                VkInstance vulkan_instance{};
                VkResult vulkan_result{};
                check(instance_, extensions_.create_vulkan_instance(create_info, vulkan_instance, vulkan_result), "xrCreateVulkanInstanceKHR");
                check_vk(vulkan_result, "xrCreateVulkanInstanceKHR Vulkan operation");
                vulkan_instance_ = vulkan_instance;
                return vulkan_instance;
            }

            std::tuple<VkPhysicalDevice, VkDevice> create_device() override {
                auto get_info = make_struct<XrVulkanGraphicsDeviceGetInfoKHR>();
                get_info.systemId = system_id_;
                get_info.vulkanInstance = vulkan_instance_;

                VkPhysicalDevice physical_device{};
                check(instance_, extensions_.get_vulkan_graphics_device(get_info, physical_device), "xrGetVulkanGraphicsDevice2KHR");

                uint32_t family_count{};
                vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &family_count, nullptr);
                std::vector<VkQueueFamilyProperties> families(family_count);
                vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &family_count, families.data());

                const vkz::queue requested_flags = queue_flags();
                const auto family = std::find_if(families.begin(), families.end(), [requested_flags](const VkQueueFamilyProperties &properties) {
                    return properties.queueCount > 0 && has_flag(static_cast<vkz::queue>(properties.queueFlags), requested_flags);
                });
                if (family == families.end()) {
                    throw std::runtime_error{"The OpenXR Vulkan device has no compatible queue family"};
                }

                const uint32_t family_index = static_cast<uint32_t>(std::distance(families.begin(), family));
                graphics_queue_family_ = family_index;
                const uint32_t queue_count = std::min(graphics_queue_count(), family->queueCount);
                std::vector<float> priorities(queue_count, 1.0F);

                auto queue_create_info = VkDeviceQueueCreateInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
                queue_create_info.queueFamilyIndex = family_index;
                queue_create_info.queueCount = queue_count;
                queue_create_info.pQueuePriorities = priorities.data();

                std::vector<VkDeviceQueueCreateInfo> queue_create_infos{queue_create_info};

                for (uint32_t bit = 1; bit != 0; bit <<= 1) {
                    const auto flag = static_cast<vkz::queue>(bit);
                    if (!has_flag(unique_queue_flags(), flag))
                        continue;

                    uint32_t additional_family = UINT32_MAX;

                    if (flag == vkz::queue_video_decode) {
                        additional_family = vkz::video::decode_queue_family(physical_device);
                    } else {
                        for (uint32_t i = 0; i < families.size(); ++i) {
                            if (families[i].queueCount > 0 && (families[i].queueFlags & bit) != 0
                                && std::none_of(queue_create_infos.begin(), queue_create_infos.end(),
                                    [i](const auto &value) { return value.queueFamilyIndex == i; })) {
                                additional_family = i;
                                break;
                            }
                        }
                    }

                    if (additional_family == UINT32_MAX)
                        throw std::runtime_error{"The OpenXR Vulkan device has no additional requested queue family"};

                    if (std::none_of(queue_create_infos.begin(), queue_create_infos.end(),
                        [additional_family](const auto &value) { return value.queueFamilyIndex == additional_family; })) {
                        auto additional_queue = VkDeviceQueueCreateInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
                        additional_queue.queueFamilyIndex = additional_family;
                        additional_queue.queueCount = 1;
                        additional_queue.pQueuePriorities = priorities.data();
                        queue_create_infos.push_back(additional_queue);
                    }
                }

                auto device_create_info = VkDeviceCreateInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
                std::vector<const char *> extensions{device_extensions().begin(), device_extensions().end()};
                if (api_version_ < VK_API_VERSION_1_3 && std::ranges::find(extensions, std::string_view{VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME}) == extensions.end()) {
                    extensions.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
                }
                const auto layers = device_layers();
                VkPhysicalDeviceDynamicRenderingFeatures dynamic_rendering{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES, const_cast<void *>(device_extension_chain()), VK_TRUE};
                device_create_info.pNext = &dynamic_rendering;
                device_create_info.queueCreateInfoCount = static_cast<uint32_t>(queue_create_infos.size());
                device_create_info.pQueueCreateInfos = queue_create_infos.data();
                device_create_info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
                device_create_info.ppEnabledExtensionNames = extensions.data();
                device_create_info.enabledLayerCount = static_cast<uint32_t>(layers.size());
                device_create_info.ppEnabledLayerNames = layers.data();
                device_create_info.pEnabledFeatures = &enabled_features();

                auto create_info = make_struct<XrVulkanDeviceCreateInfoKHR>();
                create_info.systemId = system_id_;
                create_info.pfnGetInstanceProcAddr = vkGetInstanceProcAddr;
                create_info.vulkanPhysicalDevice = physical_device;
                create_info.vulkanCreateInfo = &device_create_info;

                VkDevice device{};
                VkResult vulkan_result{};
                check(instance_, extensions_.create_vulkan_device(create_info, device, vulkan_result), "xrCreateVulkanDeviceKHR");
                check_vk(vulkan_result, "xrCreateVulkanDeviceKHR Vulkan operation");
                return {physical_device, device};
            }

          private:
            XrInstance instance_{};
            XrSystemId system_id_{};
            const extension_dispatch &extensions_;
            std::uint32_t api_version_{};
            std::uint32_t &graphics_queue_family_;
            VkInstance vulkan_instance_{};
        };

    } // namespace

    context::context(XrInstance instance, XrSystemId system_id, XrDebugUtilsMessengerEXT debug_messenger, vkz::context vulkan, std::uint32_t graphics_queue_family, VkQueue graphics_queue,
                     std::vector<std::string> enabled_openxr_extensions) noexcept
        : instance_{instance}, system_id_{system_id}, openxr_debug_messenger_{debug_messenger}, vulkan_{std::move(vulkan)}, graphics_queue_family_{graphics_queue_family},
          graphics_queue_{graphics_queue}, enabled_openxr_extensions_{std::move(enabled_openxr_extensions)} {
    }

    context::~context() {
        reset();
    }

    context::context(context &&other) noexcept
        : instance_{std::exchange(other.instance_, XR_NULL_HANDLE)}, system_id_{std::exchange(other.system_id_, XR_NULL_SYSTEM_ID)},
          openxr_debug_messenger_{std::exchange(other.openxr_debug_messenger_, XR_NULL_HANDLE)}, vulkan_{std::move(other.vulkan_)}, graphics_queue_family_{other.graphics_queue_family_},
          graphics_queue_{std::exchange(other.graphics_queue_, VK_NULL_HANDLE)}, enabled_openxr_extensions_{std::move(other.enabled_openxr_extensions_)} {
    }

    context &context::operator=(context &&other) noexcept {
        if (this == &other) {
            return *this;
        }
        reset();
        instance_ = std::exchange(other.instance_, XR_NULL_HANDLE);
        system_id_ = std::exchange(other.system_id_, XR_NULL_SYSTEM_ID);
        openxr_debug_messenger_ = std::exchange(other.openxr_debug_messenger_, XR_NULL_HANDLE);
        vulkan_ = std::move(other.vulkan_);
        graphics_queue_family_ = other.graphics_queue_family_;
        graphics_queue_ = std::exchange(other.graphics_queue_, VK_NULL_HANDLE);
        enabled_openxr_extensions_ = std::move(other.enabled_openxr_extensions_);
        return *this;
    }

    context::builder context::create() {
        return {};
    }

    XrGraphicsBindingVulkan2KHR context::graphics_binding() const noexcept {
        auto binding = make_struct<XrGraphicsBindingVulkan2KHR>();
        binding.instance = vulkan_instance();
        binding.physicalDevice = physical_device();
        binding.device = device().logical;
        binding.queueFamilyIndex = graphics_queue_family_;
        binding.queueIndex = 0;
        return binding;
    }

    bool context::openxr_extension_enabled(std::string_view extension) const noexcept {
        return std::ranges::find(enabled_openxr_extensions_, extension) != enabled_openxr_extensions_.end();
    }

    void context::reset() noexcept {
        if (openxr_debug_messenger_ != XR_NULL_HANDLE) {
            extension_dispatch{instance_}.destroy_debug_utils_messenger(openxr_debug_messenger_);
            openxr_debug_messenger_ = XR_NULL_HANDLE;
        }
        if (instance_ != XR_NULL_HANDLE) {
            xrDestroyInstance(instance_);
        }
        instance_ = XR_NULL_HANDLE;
        vulkan_ = {};
        system_id_ = XR_NULL_SYSTEM_ID;
        graphics_queue_family_ = 0;
        graphics_queue_ = VK_NULL_HANDLE;
        enabled_openxr_extensions_.clear();
    }

    context::builder::builder() {
        vkz::spdlog_adapter::install();
        add_openxr_extension(XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME);
        add_optional_openxr_extension(XR_KHR_COMPOSITION_LAYER_CUBE_EXTENSION_NAME);
        add_optional_openxr_extension(XR_KHR_COMPOSITION_LAYER_EQUIRECT_EXTENSION_NAME);
        add_optional_openxr_extension(XR_KHR_COMPOSITION_LAYER_DEPTH_EXTENSION_NAME);
#ifndef NDEBUG
        add_openxr_extension(XR_EXT_DEBUG_UTILS_EXTENSION_NAME);
        add_instance_extension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif
    }

    context::builder &context::builder::app_name(const std::string &name) {
        application_name_ = name;
        vkz::builder::app_name(name);
        return *this;
    }

    context::builder &context::builder::vulkan_api_version(std::uint32_t version) {
        vulkan_api_version_ = version;
        vkz::builder::api_version(version);
        return *this;
    }

    context::builder &context::builder::openxr_api_version(XrVersion version) {
        openxr_api_version_ = version;
        return *this;
    }

    context::builder &context::builder::form_factor(XrFormFactor form_factor) {
        form_factor_ = form_factor;
        return *this;
    }

    context::builder &context::builder::add_openxr_extension(const std::string &extension) {
        if (std::ranges::find(openxr_extensions_, extension) == openxr_extensions_.end()) {
            openxr_extensions_.push_back(extension);
        }
        return *this;
    }

    context::builder &context::builder::add_optional_openxr_extension(const std::string &extension) {
        if (std::ranges::find(optional_openxr_extensions_, extension) == optional_openxr_extensions_.end()) {
            optional_openxr_extensions_.push_back(extension);
        }
        return *this;
    }

    context::builder &context::builder::add_openxr_layer(const std::string &layer) {
        if (std::ranges::find(openxr_layers_, layer) == openxr_layers_.end()) {
            openxr_layers_.push_back(layer);
        }
        return *this;
    }

    context context::builder::build() {
        const auto available_extensions = enumerate<XrExtensionProperties>(xrEnumerateInstanceExtensionProperties, nullptr);
        auto enabled_openxr_extensions = openxr_extensions_;
        for (const auto &extension : optional_openxr_extensions_) {
            const bool supported = std::ranges::any_of(available_extensions, [&](const XrExtensionProperties &properties) {
                return extension == properties.extensionName;
            });
            if (supported && std::ranges::find(enabled_openxr_extensions, extension) == enabled_openxr_extensions.end()) {
                enabled_openxr_extensions.push_back(extension);
            }
        }

        std::vector<const char *> extension_names;
        extension_names.reserve(enabled_openxr_extensions.size());
        for (const auto &extension : enabled_openxr_extensions) {
            extension_names.push_back(extension.c_str());
        }

        std::vector<const char *> layer_names;
        layer_names.reserve(openxr_layers_.size());
        for (const auto &layer : openxr_layers_)
            layer_names.push_back(layer.c_str());

        auto create_info = make_struct<XrInstanceCreateInfo>();
        create_info.applicationInfo.apiVersion = openxr_api_version_;
        std::strncpy(create_info.applicationInfo.applicationName, application_name_.c_str(), XR_MAX_APPLICATION_NAME_SIZE - 1);
        create_info.enabledExtensionCount = static_cast<uint32_t>(extension_names.size());
        create_info.enabledExtensionNames = extension_names.data();
        create_info.enabledApiLayerCount = static_cast<uint32_t>(layer_names.size());
        create_info.enabledApiLayerNames = layer_names.data();

#ifndef NDEBUG
        auto debug_create_info = openxr_debug_create_info();
        create_info.next = &debug_create_info;
#endif

        XrInstance instance{};
        check(XR_NULL_HANDLE, xrCreateInstance(&create_info, &instance), "xrCreateInstance");

        XrDebugUtilsMessengerEXT debug_messenger{XR_NULL_HANDLE};
        try {
            auto system_info = make_struct<XrSystemGetInfo>();
            system_info.formFactor = form_factor_;
            XrSystemId system_id{};
            check(instance, xrGetSystem(instance, &system_info, &system_id), "xrGetSystem");

            extension_dispatch extensions{instance};
#ifndef NDEBUG
            check(instance, extensions.create_debug_utils_messenger(debug_create_info, debug_messenger), "xrCreateDebugUtilsMessengerEXT");
#endif
            auto requirements = make_struct<XrGraphicsRequirementsVulkan2KHR>();
            check(instance, extensions.get_vulkan_graphics_requirements(system_id, requirements), "xrGetVulkanGraphicsRequirements2KHR");
            const XrVersion requested_vulkan_version = XR_MAKE_VERSION(VK_API_VERSION_MAJOR(vulkan_api_version_), VK_API_VERSION_MINOR(vulkan_api_version_), VK_API_VERSION_PATCH(vulkan_api_version_));
            if (requested_vulkan_version < requirements.minApiVersionSupported || requested_vulkan_version > requirements.maxApiVersionSupported) {
                throw std::runtime_error{"The requested Vulkan API version is outside the range supported by the OpenXR runtime"};
            }

            std::uint32_t graphics_queue_family{};
            openxr_device_factory factory{instance, system_id, extensions, vulkan_api_version_, graphics_queue_family};
            vkz::builder::device_factory(factory);
            auto vulkan = vkz::builder::build();
            VkQueue graphics_queue{VK_NULL_HANDLE};
            vkGetDeviceQueue(vulkan.device.logical, graphics_queue_family, 0, &graphics_queue);
            return context{instance, system_id, debug_messenger, std::move(vulkan), graphics_queue_family, graphics_queue, std::move(enabled_openxr_extensions)};
        } catch (...) {
            if (debug_messenger != XR_NULL_HANDLE) {
                extension_dispatch{instance}.destroy_debug_utils_messenger(debug_messenger);
            }
            xrDestroyInstance(instance);
            throw;
        }
    }

} // namespace vxr
