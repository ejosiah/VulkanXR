#include <vxr/vulkanxr.hpp>

#include <vulkanizer/application.hpp>

#include <imgui.h>
#include <openxr/openxr.h>
#include <spdlog/spdlog.h>

#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

template <typename Property, typename Enumerate>
std::vector<Property> enumerate_properties(XrStructureType type, Enumerate&& enumerate) {
    uint32_t count = 0;
    XrResult result = enumerate(0, &count, nullptr);
    if (XR_FAILED(result)) {
        throw std::runtime_error{"OpenXR property count enumeration failed"};
    }

    std::vector<Property> properties(count);
    for (auto& property : properties) {
        property.type = type;
        property.next = nullptr;
    }

    result = enumerate(count, &count, properties.data());
    if (XR_FAILED(result)) {
        throw std::runtime_error{"OpenXR property enumeration failed"};
    }
    properties.resize(count);
    return properties;
}

class openxr_info_application final : public vkz::application {
public:
    openxr_info_application()
        : application{vkz::vulkan_app_create_info{
              .width = 1200,
              .height = 720,
              .title = "OpenXR Info",
              .resizable = true,
              .validation = false,
              .vsync = true,
          }} {}

    void setup() override {
        ImGui::GetIO().FontGlobalScale = 1.35f;

        api_layers_ = enumerate_properties<XrApiLayerProperties>(
            XR_TYPE_API_LAYER_PROPERTIES,
            [](uint32_t capacity, uint32_t* count, XrApiLayerProperties* properties) {
                return xrEnumerateApiLayerProperties(capacity, count, properties);
            });

        instance_extensions_ = enumerate_properties<XrExtensionProperties>(
            XR_TYPE_EXTENSION_PROPERTIES,
            [](uint32_t capacity, uint32_t* count, XrExtensionProperties* properties) {
                return xrEnumerateInstanceExtensionProperties(nullptr, capacity, count, properties);
            });
    }

    void new_frame() override {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);

        constexpr ImGuiWindowFlags window_flags =
            ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoSavedSettings;

        ImGui::Begin("OpenXR Info", nullptr, window_flags);
        ImGui::Text("OpenXR API version: %s", vxr::version_to_string(vxr::openxr_api_version()).c_str());
        ImGui::Separator();

        if (ImGui::BeginTabBar("OpenXR properties")) {
            if (ImGui::BeginTabItem("API Layers")) {
                draw_api_layers();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Instance Extensions")) {
                draw_instance_extensions();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::End();
    }

    void app_logic(VkCommandBuffer) override {}

private:
    void draw_api_layers() const {
        ImGui::Text("%zu API layers", api_layers_.size());
        constexpr ImGuiTableFlags flags =
            ImGuiTableFlags_Borders |
            ImGuiTableFlags_RowBg |
            ImGuiTableFlags_Resizable |
            ImGuiTableFlags_ScrollY;

        if (!ImGui::BeginTable("API layers", 4, flags, ImVec2{0.0f, -1.0f})) return;
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 1.2f);
        ImGui::TableSetupColumn("Description", ImGuiTableColumnFlags_WidthStretch, 1.5f);
        ImGui::TableSetupColumn("Layer Version", ImGuiTableColumnFlags_WidthFixed, 110.0f);
        ImGui::TableSetupColumn("Spec Version", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableHeadersRow();

        for (const auto& layer : api_layers_) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(layer.layerName);
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(layer.description);
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%u", layer.layerVersion);
            ImGui::TableSetColumnIndex(3);
            const std::string spec_version = vxr::version_to_string(layer.specVersion);
            ImGui::TextUnformatted(spec_version.c_str());
        }
        ImGui::EndTable();
    }

    void draw_instance_extensions() const {
        ImGui::Text("%zu instance extensions", instance_extensions_.size());
        constexpr ImGuiTableFlags flags =
            ImGuiTableFlags_Borders |
            ImGuiTableFlags_RowBg |
            ImGuiTableFlags_Resizable |
            ImGuiTableFlags_ScrollY;

        if (!ImGui::BeginTable("Instance extensions", 2, flags, ImVec2{0.0f, -1.0f})) return;
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Extension Version", ImGuiTableColumnFlags_WidthFixed, 140.0f);
        ImGui::TableHeadersRow();

        for (const auto& extension : instance_extensions_) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(extension.extensionName);
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%u", extension.extensionVersion);
        }
        ImGui::EndTable();
    }

    std::vector<XrApiLayerProperties> api_layers_;
    std::vector<XrExtensionProperties> instance_extensions_;
};

} // namespace

int main() {
    try {
        vkz::spdlog_adapter::install();
        openxr_info_application application;
        application.main_loop();
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        spdlog::critical("OpenXR Info failed: {}", error.what());
        return EXIT_FAILURE;
    }
}
