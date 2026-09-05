#include <vxr/vxr.hpp>

#include <vulkanizer/application.hpp>

#include <imgui.h>
#include <spdlog/spdlog.h>
#include <vulkan/vulkan.hpp>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

    constexpr const char *runtime_unavailable_message = "This information is unavailable. Make sure an OpenXR runtime is running and a headset is connected and ready, then restart OpenXR Info.";

    template <typename Property, typename Enumerate> std::vector<Property> enumerate_properties(XrStructureType type, Enumerate &&enumerate) {
        uint32_t count{};
        XrResult result = enumerate(0, &count, nullptr);
        if (XR_FAILED(result)) throw vxr::openxr_error{"OpenXR property count enumeration", result};

        std::vector<Property> properties(count);
        for (auto &property : properties) {
            property.type = type;
            property.next = nullptr;
        }
        result = enumerate(count, &count, properties.data());
        if (XR_FAILED(result)) throw vxr::openxr_error{"OpenXR property enumeration", result};
        properties.resize(count);
        return properties;
    }

    template <typename Value, typename Enumerate> std::vector<Value> enumerate_values(Enumerate &&enumerate) {
        uint32_t count{};
        XrResult result = enumerate(0, &count, nullptr);
        if (XR_FAILED(result)) throw vxr::openxr_error{"OpenXR enumeration count", result};

        std::vector<Value> values(count);
        result = enumerate(count, &count, values.data());
        if (XR_FAILED(result)) throw vxr::openxr_error{"OpenXR enumeration", result};
        values.resize(count);
        return values;
    }

    std::string unavailable_message(const std::exception &error) {
        return runtime_unavailable_message + std::string{"\n\nDetails: "} + error.what();
    }

    std::string unavailable_message(const char *operation, XrResult result) {
        return runtime_unavailable_message + std::string{"\n\nDetails: "} + operation + " failed with XrResult " + std::to_string(result);
    }

    const char *reference_space_name(XrReferenceSpaceType space) {
        switch (space) {
        case XR_REFERENCE_SPACE_TYPE_VIEW:
            return "XR_REFERENCE_SPACE_TYPE_VIEW";
        case XR_REFERENCE_SPACE_TYPE_LOCAL:
            return "XR_REFERENCE_SPACE_TYPE_LOCAL";
        case XR_REFERENCE_SPACE_TYPE_STAGE:
            return "XR_REFERENCE_SPACE_TYPE_STAGE";
        case XR_REFERENCE_SPACE_TYPE_LOCAL_FLOOR_EXT:
            return "XR_REFERENCE_SPACE_TYPE_LOCAL_FLOOR_EXT";
        case XR_REFERENCE_SPACE_TYPE_UNBOUNDED_MSFT:
            return "XR_REFERENCE_SPACE_TYPE_UNBOUNDED_MSFT";
        default:
            return "XR_REFERENCE_SPACE_TYPE_UNKNOWN";
        }
    }

    std::string swapchain_format_name(int64_t format) {
        return vk::to_string(static_cast<vk::Format>(format));
    }

    struct openxr_info_data {
        std::vector<XrApiLayerProperties> api_layers;
        std::vector<XrExtensionProperties> instance_extensions;
        XrInstanceProperties runtime_properties{};
        XrSystemProperties system_properties{};
        std::vector<XrViewConfigurationType> view_configurations;
        std::vector<std::vector<XrViewConfigurationView>> view_configuration_views;
        std::vector<std::vector<XrEnvironmentBlendMode>> blend_modes;
        std::vector<XrReferenceSpaceType> reference_spaces;
        std::vector<int64_t> swapchain_formats;
        std::vector<std::string> bound_sources;
        std::vector<std::uintptr_t> swapchain_images;
        std::string api_layers_error;
        std::string instance_extensions_error;
        std::string runtime_error;
        std::string system_properties_error;
        std::string system_error;
        std::string session_error;
        std::string reference_spaces_error;
        std::string swapchain_formats_error;
        std::string bound_sources_error;
        std::string swapchain_images_error;
    };

    struct openxr_handles {
        XrSession session{XR_NULL_HANDLE};
        XrActionSet action_set{XR_NULL_HANDLE};
        XrAction action{XR_NULL_HANDLE};

        ~openxr_handles() {
            if (action != XR_NULL_HANDLE) xrDestroyAction(action);
            if (action_set != XR_NULL_HANDLE) xrDestroyActionSet(action_set);
            if (session != XR_NULL_HANDLE) xrDestroySession(session);
        }
    };

    std::string path_to_string(XrInstance instance, XrPath path) {
        uint32_t size{};
        if (XR_FAILED(xrPathToString(instance, path, 0, &size, nullptr))) return std::to_string(path);
        std::string value(size, '\0');
        if (XR_FAILED(xrPathToString(instance, path, size, &size, value.data()))) return std::to_string(path);
        if (!value.empty()) value.pop_back();
        return value;
    }

    void query_action_sources(openxr_info_data &data, vxr::context &context, openxr_handles &handles) {
        auto action_set_info = vxr::make_struct<XrActionSetCreateInfo>();
        std::strncpy(action_set_info.actionSetName, "openxr_info", XR_MAX_ACTION_SET_NAME_SIZE - 1);
        std::strncpy(action_set_info.localizedActionSetName, "OpenXR Info", XR_MAX_LOCALIZED_ACTION_SET_NAME_SIZE - 1);
        XrResult result = xrCreateActionSet(context.instance(), &action_set_info, &handles.action_set);
        if (XR_FAILED(result)) throw vxr::openxr_error{"xrCreateActionSet", result};

        auto action_info = vxr::make_struct<XrActionCreateInfo>();
        action_info.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
        std::strncpy(action_info.actionName, "sample_action", XR_MAX_ACTION_NAME_SIZE - 1);
        std::strncpy(action_info.localizedActionName, "Sample Action", XR_MAX_LOCALIZED_ACTION_NAME_SIZE - 1);
        result = xrCreateAction(handles.action_set, &action_info, &handles.action);
        if (XR_FAILED(result)) throw vxr::openxr_error{"xrCreateAction", result};

        auto attach_info = vxr::make_struct<XrSessionActionSetsAttachInfo>();
        attach_info.countActionSets = 1;
        attach_info.actionSets = &handles.action_set;
        result = xrAttachSessionActionSets(handles.session, &attach_info);
        if (XR_FAILED(result)) throw vxr::openxr_error{"xrAttachSessionActionSets", result};

        auto enumerate_info = vxr::make_struct<XrBoundSourcesForActionEnumerateInfo>();
        enumerate_info.action = handles.action;
        const auto paths = enumerate_values<XrPath>([&](uint32_t capacity, uint32_t *count, XrPath *values) {
            return xrEnumerateBoundSourcesForAction(handles.session, &enumerate_info, capacity, count, values);
        });
        for (const auto path : paths)
            data.bound_sources.push_back(path_to_string(context.instance(), path));
    }

    void query_swapchain_images(openxr_info_data &data, openxr_handles &handles) {
        if (data.swapchain_formats.empty() || data.view_configuration_views.empty() || data.view_configuration_views.front().empty())
            throw std::runtime_error{"The runtime returned no usable swapchain format or view configuration"};

        const auto &view = data.view_configuration_views.front().front();
        vxr::swapchain swapchain{
            handles.session,
            vxr::swapchain_create_info{
                .name_ = "openxr_info",
                .usage_flags_ = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT,
                .format_ = static_cast<VkFormat>(data.swapchain_formats.front()),
                .sample_count_ = view.recommendedSwapchainSampleCount,
                .width_ = view.recommendedImageRectWidth,
                .height_ = view.recommendedImageRectHeight,
            },
        };
        for (const auto &image : swapchain.images())
            data.swapchain_images.push_back(reinterpret_cast<std::uintptr_t>(image.image));
        swapchain.destroy();
    }

    openxr_info_data query_openxr() {
        openxr_info_data data;
        try {
            data.api_layers = enumerate_properties<XrApiLayerProperties>(XR_TYPE_API_LAYER_PROPERTIES, [](uint32_t capacity, uint32_t *count, XrApiLayerProperties *properties) {
                return xrEnumerateApiLayerProperties(capacity, count, properties);
            });
        } catch (const std::exception &error) {
            data.api_layers_error = unavailable_message(error);
        }

        try {
            data.instance_extensions = enumerate_properties<XrExtensionProperties>(XR_TYPE_EXTENSION_PROPERTIES, [](uint32_t capacity, uint32_t *count, XrExtensionProperties *properties) {
                return xrEnumerateInstanceExtensionProperties(nullptr, capacity, count, properties);
            });
        } catch (const std::exception &error) {
            data.instance_extensions_error = unavailable_message(error);
        }

        try {
            auto context = vxr::context::create().app_name("OpenXR Info").openxr_api_version(
                    XR_API_VERSION_1_0).vulkan_api_version(VK_API_VERSION_1_2).build();
            openxr_handles handles;

            try {
                data.runtime_properties = vxr::make_struct<XrInstanceProperties>();
                vxr::check(context.instance(), xrGetInstanceProperties(context.instance(), &data.runtime_properties), "xrGetInstanceProperties");
            } catch (const std::exception &error) {
                data.runtime_error = unavailable_message(error);
            }

            try {
                data.system_properties = vxr::make_struct<XrSystemProperties>();
                vxr::check(context.instance(), xrGetSystemProperties(context.instance(), context.system_id(), &data.system_properties), "xrGetSystemProperties");
            } catch (const std::exception &error) {
                data.system_properties_error = unavailable_message(error);
            }

            data.view_configurations = enumerate_values<XrViewConfigurationType>([&](uint32_t capacity, uint32_t *count, XrViewConfigurationType *values) {
                return xrEnumerateViewConfigurations(context.instance(), context.system_id(), capacity, count, values);
            });
            for (const auto configuration : data.view_configurations) {
                data.view_configuration_views.push_back(
                    enumerate_properties<XrViewConfigurationView>(XR_TYPE_VIEW_CONFIGURATION_VIEW, [&](uint32_t capacity, uint32_t *count, XrViewConfigurationView *values) {
                        return xrEnumerateViewConfigurationViews(context.instance(), context.system_id(), configuration, capacity, count, values);
                    }));
                data.blend_modes.push_back(enumerate_values<XrEnvironmentBlendMode>([&](uint32_t capacity, uint32_t *count, XrEnvironmentBlendMode *values) {
                    return xrEnumerateEnvironmentBlendModes(context.instance(), context.system_id(), configuration, capacity, count, values);
                }));
            }

            auto binding = context.graphics_binding();
            auto session_info = vxr::make_struct<XrSessionCreateInfo>();
            session_info.next = &binding;
            session_info.systemId = context.system_id();
            const XrResult session_result = xrCreateSession(context.instance(), &session_info, &handles.session);
            if (XR_FAILED(session_result)) {
                data.session_error = unavailable_message("xrCreateSession", session_result);
                return data;
            }

            try {
                data.reference_spaces = enumerate_values<XrReferenceSpaceType>([&](uint32_t capacity, uint32_t *count, XrReferenceSpaceType *values) {
                    return xrEnumerateReferenceSpaces(handles.session, capacity, count, values);
                });
            } catch (const std::exception &error) {
                data.reference_spaces_error = unavailable_message(error);
            }

            try {
                data.swapchain_formats = enumerate_values<int64_t>([&](uint32_t capacity, uint32_t *count, int64_t *values) {
                    return xrEnumerateSwapchainFormats(handles.session, capacity, count, values);
                });
            } catch (const std::exception &error) {
                data.swapchain_formats_error = unavailable_message(error);
            }

            try {
                query_action_sources(data, context, handles);
            } catch (const std::exception &error) {
                data.bound_sources_error = unavailable_message(error);
            }

            try {
                query_swapchain_images(data, handles);
            } catch (const std::exception &error) {
                data.swapchain_images_error = unavailable_message(error);
            }
        } catch (const std::exception &error) {
            data.system_error = unavailable_message(error);
            data.runtime_error = data.system_error;
            data.system_properties_error = data.system_error;
        }
        return data;
    }

    void unavailable(const std::string &message) {
        ImGui::Spacing();
        ImGui::TextWrapped("%s", message.c_str());
    }

    class openxr_info_application final : public vkz::application {
      public:
        explicit openxr_info_application(openxr_info_data data)
            : application{vkz::vulkan_app_create_info{.width = 1200, .height = 720, .title = "OpenXR Info", .api_version = VK_API_VERSION_1_2, .resizable = true, .validation = false, .vsync = true}},
              data_{std::move(data)} {
        }

        void setup() override {
            ImGui::GetIO().FontGlobalScale = 1.35f;
        }

        void new_frame() override {
            const ImGuiViewport *viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(viewport->WorkPos);
            ImGui::SetNextWindowSize(viewport->WorkSize);
            ImGui::Begin("OpenXR Info", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
            ImGui::Text("OpenXR API version: %s", vxr::version_to_string(vxr::openxr_api_version()).c_str());
            ImGui::Separator();
            if (ImGui::BeginTabBar("OpenXR properties")) {
                draw_tab("Runtime Info", data_.runtime_error, [&] {
                    draw_runtime_info();
                });
                draw_tab("System Properties", data_.system_properties_error, [&] {
                    draw_system_properties();
                });
                draw_tab("API Layers", data_.api_layers_error, [&] {
                    draw_api_layers();
                });
                draw_tab("Instance Extensions", data_.instance_extensions_error, [&] {
                    draw_instance_extensions();
                });
                draw_tab("View Configurations", data_.system_error, [&] {
                    draw_view_configurations();
                });
                draw_tab("Blend Modes", data_.system_error, [&] {
                    draw_blend_modes();
                });
                draw_tab("Reference Spaces", session_error(data_.reference_spaces_error), [&] {
                    draw_reference_spaces();
                });
                draw_tab("Swapchain Formats", session_error(data_.swapchain_formats_error), [&] {
                    draw_swapchain_formats();
                });
                draw_tab("Bound Sources", session_error(data_.bound_sources_error), [&] {
                    draw_bound_sources();
                });
                draw_tab("Swapchain Images", session_error(data_.swapchain_images_error), [&] {
                    draw_swapchain_images();
                });
                ImGui::EndTabBar();
            }
            ImGui::End();
        }

        void app_logic(VkCommandBuffer) override {
        }

      private:
        template <typename Draw> void draw_tab(const char *name, const std::string &error, Draw &&draw) const {
            if (!ImGui::BeginTabItem(name)) return;
            if (!error.empty())
                unavailable(error);
            else
                draw();
            ImGui::EndTabItem();
        }

        const std::string &session_error(const std::string &specific_error) const {
            if (!data_.system_error.empty()) return data_.system_error;
            if (!data_.session_error.empty()) return data_.session_error;
            return specific_error;
        }

        void draw_runtime_info() const {
            ImGui::Text("Runtime Name: %s", data_.runtime_properties.runtimeName);
            ImGui::Text("Runtime Version: %s", vxr::version_to_string(data_.runtime_properties.runtimeVersion).c_str());
            ImGui::Text("OpenXR API Version: %s", vxr::version_to_string(vxr::openxr_api_version()).c_str());
        }

        void draw_system_properties() const {
            ImGui::Text("System ID: %llu", static_cast<unsigned long long>(data_.system_properties.systemId));
            ImGui::Text("System Name: %s", data_.system_properties.systemName);
            ImGui::Text("Vendor ID: %u", data_.system_properties.vendorId);
            ImGui::SeparatorText("Graphics");
            ImGui::Text("Maximum Swapchain Image Width: %u", data_.system_properties.graphicsProperties.maxSwapchainImageWidth);
            ImGui::Text("Maximum Swapchain Image Height: %u", data_.system_properties.graphicsProperties.maxSwapchainImageHeight);
            ImGui::Text("Maximum Swapchain Layer Count: %u", data_.system_properties.graphicsProperties.maxLayerCount);
            ImGui::SeparatorText("Tracking");
            ImGui::Text("Orientation Tracking: %s", data_.system_properties.trackingProperties.orientationTracking == XR_TRUE ? "Supported" : "Not supported");
            ImGui::Text("Position Tracking: %s", data_.system_properties.trackingProperties.positionTracking == XR_TRUE ? "Supported" : "Not supported");
        }

        void draw_api_layers() const {
            ImGui::Text("%zu API layers", data_.api_layers.size());
            if (!ImGui::BeginTable("API layers", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY, ImVec2{0.0f, -1.0f})) return;
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 1.2f);
            ImGui::TableSetupColumn("Description", ImGuiTableColumnFlags_WidthStretch, 1.5f);
            ImGui::TableSetupColumn("Layer Version", ImGuiTableColumnFlags_WidthFixed, 110.0f);
            ImGui::TableSetupColumn("Spec Version", ImGuiTableColumnFlags_WidthFixed, 100.0f);
            ImGui::TableHeadersRow();
            for (const auto &layer : data_.api_layers) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(layer.layerName);
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(layer.description);
                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%u", layer.layerVersion);
                ImGui::TableSetColumnIndex(3);
                ImGui::TextUnformatted(vxr::version_to_string(layer.specVersion).c_str());
            }
            ImGui::EndTable();
        }

        void draw_instance_extensions() const {
            ImGui::Text("%zu instance extensions", data_.instance_extensions.size());
            if (!ImGui::BeginTable("Instance extensions", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY, ImVec2{0.0f, -1.0f})) return;
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Extension Version", ImGuiTableColumnFlags_WidthFixed, 140.0f);
            ImGui::TableHeadersRow();
            for (const auto &extension : data_.instance_extensions) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(extension.extensionName);
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%u", extension.extensionVersion);
            }
            ImGui::EndTable();
        }

        void draw_view_configurations() const {
            for (std::size_t index = 0; index < data_.view_configurations.size(); ++index) {
                ImGui::SeparatorText(vxr::to_string(data_.view_configurations[index]).data());
                for (std::size_t view_index = 0; view_index < data_.view_configuration_views[index].size(); ++view_index) {
                    const auto &view = data_.view_configuration_views[index][view_index];
                    ImGui::Text("View %zu: recommended %ux%u @ %u samples, maximum %ux%u @ %u samples", view_index, view.recommendedImageRectWidth, view.recommendedImageRectHeight,
                                view.recommendedSwapchainSampleCount, view.maxImageRectWidth, view.maxImageRectHeight, view.maxSwapchainSampleCount);
                }
            }
        }

        void draw_blend_modes() const {
            for (std::size_t index = 0; index < data_.view_configurations.size(); ++index) {
                ImGui::SeparatorText(vxr::to_string(data_.view_configurations[index]).data());
                for (const auto mode : data_.blend_modes[index])
                    ImGui::BulletText("%s", vxr::to_string(mode).data());
            }
        }

        void draw_reference_spaces() const {
            for (const auto space : data_.reference_spaces)
                ImGui::BulletText("%s (%d)", reference_space_name(space), static_cast<int>(space));
        }

        void draw_swapchain_formats() const {
            for (const auto format : data_.swapchain_formats) {
                const std::string name = swapchain_format_name(format);
                ImGui::BulletText("%s (%lld)", name.c_str(), static_cast<long long>(format));
            }
        }

        void draw_bound_sources() const {
            if (data_.bound_sources.empty()) ImGui::TextWrapped("No input sources are currently bound to the sample action.");
            for (const auto &source : data_.bound_sources)
                ImGui::BulletText("%s", source.c_str());
        }

        void draw_swapchain_images() const {
            for (std::size_t index = 0; index < data_.swapchain_images.size(); ++index)
                ImGui::BulletText("Image %zu: 0x%llx", index, static_cast<unsigned long long>(data_.swapchain_images[index]));
        }

        openxr_info_data data_;
    };

} // namespace

int main() {
    try {
        auto data = query_openxr();
        openxr_info_application application{std::move(data)};
        application.main_loop();
        return EXIT_SUCCESS;
    } catch (const std::exception &error) {
        spdlog::critical("OpenXR Info failed: {}", error.what());
        return EXIT_FAILURE;
    }
}
