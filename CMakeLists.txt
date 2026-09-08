cmake_minimum_required(VERSION 3.24)

project(VulkanXR VERSION 0.1.0 LANGUAGES CXX)

option(VULKANXR_BUILD_EXAMPLES "Build VulkanXR examples" ON)

include(FetchContent)
FetchContent_Declare(
    cmrc
    GIT_REPOSITORY https://github.com/vector-of-bool/cmrc.git
    GIT_TAG 2.0.1
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(cmrc)

# Keep CLion's direct build directories compatible with Vista's Conan layout.
set(VULKANXR_CONAN_GENERATORS "${CMAKE_BINARY_DIR}/build/generators")
if(EXISTS "${VULKANXR_CONAN_GENERATORS}/vulkanizer-config.cmake")
    list(PREPEND CMAKE_PREFIX_PATH "${VULKANXR_CONAN_GENERATORS}")
endif()

find_package(vulkanizer CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(OPENXR 1.1.62 CONFIG REQUIRED)

file(GLOB_RECURSE VULKANXR_SHADER_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/*"
)
cmrc_add_resource_library(vulkanxr_shaders
    ALIAS VulkanXR::shaders
    NAMESPACE vxr_shaders
    WHENCE "${CMAKE_CURRENT_SOURCE_DIR}/src/shaders"
    ${VULKANXR_SHADER_FILES}
)

add_library(vulkanxr STATIC
    src/vxr/application.hpp
    src/vxr/application_builder.hpp
    src/vxr/app.hpp
    src/vxr/desktop_application.hpp
    src/vxr/desktop_context.hpp
    src/vxr/desktop_input.hpp
    src/vxr/backoff.hpp
    src/vxr/action.hpp
    src/vxr/vxr.hpp
    src/vxr/openxr_helpers.hpp
    src/vxr/status.hpp
    src/vxr/renderer.hpp
    src/vxr/rendering_context.hpp
    src/vxr/input.hpp
    src/vxr/session_context.hpp
    src/vxr/session_config.hpp
    src/vxr/session_service.hpp
    src/vxr/swapchain.hpp
    src/vxr/extension_dispatch.hpp
    src/vxr/font_pipeline.hpp
    src/vxr/mirror.hpp
    src/vxr/context.hpp
    src/vxr/models.hpp
    src/vxr/transforms.hpp
    src/application.cpp
    src/application_builder.cpp
    src/desktop_application.cpp
    src/desktop_input.cpp
    src/backoff.cpp
    src/vulkanxr.cpp
    src/extension_dispatch.cpp
    src/font_pipeline.cpp
    src/context.cpp
    src/input.cpp
    src/mirror.cpp
    src/session_service.cpp
    src/swapchain.cpp
)
add_library(VulkanXR::VulkanXR ALIAS vulkanxr)

target_compile_features(vulkanxr PUBLIC cxx_std_20)
target_link_libraries(vulkanxr
    PUBLIC
        vulkanizer::vulkanizer
        glm::glm-header-only
        OpenXR::openxr_loader
    PRIVATE
        VulkanXR::shaders
        spdlog::spdlog
)
target_include_directories(vulkanxr
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)
set_target_properties(vulkanxr PROPERTIES CXX_EXTENSIONS OFF)
target_compile_definitions(vulkanxr
    PUBLIC
        XR_USE_GRAPHICS_API_VULKAN
    PRIVATE
        VKZ_SPD_LOG_ADAPTOR
)

if(VULKANXR_BUILD_EXAMPLES)
    add_executable(vulkanxr_checkerboard_example
        example/checkerboard_renderer.cpp
        example/checkerboard_renderer.hpp
    )
    target_link_libraries(vulkanxr_checkerboard_example PRIVATE VulkanXR::VulkanXR spdlog::spdlog)

    add_executable(vulkanxr_space_visualization_example
        example/space_visualization_renderer.cpp
        example/space_visualization_renderer.hpp
    )
    target_link_libraries(vulkanxr_space_visualization_example PRIVATE VulkanXR::VulkanXR VulkanXR::shaders spdlog::spdlog)

    add_executable(vulkanxr_controller_example
        example/controller_renderer.cpp
        example/controller_renderer.hpp
    )
    target_link_libraries(vulkanxr_controller_example PRIVATE VulkanXR::VulkanXR VulkanXR::shaders spdlog::spdlog)

    add_executable(vulkanxr_movement_example
        example/movement_renderer.cpp
        example/movement_renderer.hpp
    )
    target_compile_definitions(vulkanxr_movement_example PRIVATE VULKANXR_RESOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/resources")
    target_link_libraries(vulkanxr_movement_example PRIVATE VulkanXR::VulkanXR VulkanXR::shaders spdlog::spdlog)
    add_subdirectory(openxr_info)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(vulkanxr_tests test/vulkanxr_test.cpp)
    target_link_libraries(vulkanxr_tests PRIVATE VulkanXR::VulkanXR)
    add_test(NAME vulkanxr.smoke COMMAND vulkanxr_tests)

    add_executable(vulkanxr_context_tests test/context_test.cpp)
    target_link_libraries(vulkanxr_context_tests PRIVATE VulkanXR::VulkanXR spdlog::spdlog)
    add_test(NAME vulkanxr.context COMMAND vulkanxr_context_tests)
endif()
