project(openxr_info LANGUAGES CXX)

add_executable(openxr_info main.cpp)
target_compile_features(openxr_info PRIVATE cxx_std_20)
target_compile_definitions(openxr_info PRIVATE VKZ_SPD_LOG_ADAPTOR)
target_link_libraries(openxr_info
    PRIVATE
        VulkanXR::VulkanXR
        vulkanizer::vulkanizer
        imgui::imgui
        spdlog::spdlog
)

set_target_properties(openxr_info PROPERTIES CXX_EXTENSIONS OFF)
