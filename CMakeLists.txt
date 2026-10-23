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

find_package(vulkanizer 1.0.1 CONFIG REQUIRED)
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

file(GLOB_RECURSE VULKANXR_SOURCE_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/vxr/*.hpp"
)
add_library(vulkanxr STATIC ${VULKANXR_SOURCE_FILES})
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
    target_compile_definitions(vulkanxr_movement_example PRIVATE VULKANXR_RESOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/resources"
        VULKANXR_CAT_VIDEO="${CMAKE_CURRENT_SOURCE_DIR}/../vulkanizer/test/resources/video/3116737-hd_1920_1080_25fps.mp4")
    target_link_libraries(vulkanxr_movement_example PRIVATE VulkanXR::VulkanXR VulkanXR::shaders spdlog::spdlog)

    add_executable(vulkanxr_sponza_example
        example/sponza_renderer.cpp
        example/sponza_renderer.hpp
        example/sponza_lighting.hpp
        example/rtx_tv.cpp
        example/rtx_material.cpp
        example/rtx_material.hpp
        example/rtx_neural_cache.cpp
        example/rtx_nrc.cpp
        example/rtx_nrd.cpp
    )
    target_compile_definitions(vulkanxr_sponza_example PRIVATE VULKANXR_RESOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/resources"
        VULKANXR_CAT_VIDEO="${CMAKE_CURRENT_SOURCE_DIR}/../vulkanizer/test/resources/video/3116737-hd_1920_1080_25fps.mp4")
    target_link_libraries(vulkanxr_sponza_example PRIVATE VulkanXR::VulkanXR VulkanXR::shaders spdlog::spdlog)
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/sponza_sdk.cmake")
    target_link_libraries(vulkanxr_sponza_example PRIVATE vulkanxr_nrc vulkanxr_nrd)
    foreach(runtime IN ITEMS NRC_Vulkan.dll cudart64_13.dll nvrtc64_130_0.dll nvrtc-builtins64_133.dll)
        add_custom_command(TARGET vulkanxr_sponza_example POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${VULKANXR_NRC_SDK_ROOT}/Bin/${runtime}" "$<TARGET_FILE_DIR:vulkanxr_sponza_example>")
    endforeach()
    find_program(VULKANXR_GLSLC glslc HINTS "$ENV{VULKAN_SDK}/Bin" REQUIRED)
    set(SPONZA_SHADER_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/example/shaders/sponza")
    set(SPONZA_SHADER_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/sponza_shaders")
    file(MAKE_DIRECTORY "${SPONZA_SHADER_BINARY_DIR}")
    file(GLOB_RECURSE SPONZA_SHADER_INCLUDES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/example/shaders/*.glsl")
    set(SPONZA_SHADER_OUTPUTS)
    foreach(shader IN ITEMS tv_filter.comp sponza.vert sponza.frag gbuffer.frag tone_mapping.vert tone_mapping.frag probes.vert probes.frag nrc.rgen nrc.rchit nrc.rmiss nrc.rahit nrc_resolve.comp nrd_prepare.comp nrd_resolve.comp)
        set(source "${SPONZA_SHADER_SOURCE_DIR}/${shader}")
        set(shader_options)
        if(shader STREQUAL "gbuffer.frag")
            set(source "${SPONZA_SHADER_SOURCE_DIR}/sponza.frag")
            list(APPEND shader_options -DGBUFFER_PASS)
        endif()
        set(output "${SPONZA_SHADER_BINARY_DIR}/${shader}.spv")
        add_custom_command(OUTPUT "${output}"
            COMMAND "${VULKANXR_GLSLC}" --target-env=vulkan1.2 ${shader_options} "${source}" -o "${output}"
            DEPENDS "${source}" ${SPONZA_SHADER_INCLUDES} VERBATIM)
        list(APPEND SPONZA_SHADER_OUTPUTS "${output}")
    endforeach()
    foreach(entry IN ITEMS train_bridge query_bridge)
        set(output "${SPONZA_SHADER_BINARY_DIR}/nrc_${entry}.spv")
        add_custom_command(OUTPUT "${output}"
            COMMAND "${VULKANXR_DXC}" -spirv -fspv-entrypoint-name=main -T cs_6_6 -E "${entry}" -HV 2021
                -fspv-target-env=vulkan1.2 -fvk-use-dx-layout -enable-16bit-types
                "-I${VULKANXR_NRC_SDK_ROOT}/Include" "${SPONZA_SHADER_SOURCE_DIR}/nrc_bridge.hlsl" -Fo "${output}"
            DEPENDS "${SPONZA_SHADER_SOURCE_DIR}/nrc_bridge.hlsl" "${VULKANXR_NRC_SDK_ROOT}/Include/Nrc.hlsli" VERBATIM)
        list(APPEND SPONZA_SHADER_OUTPUTS "${output}")
    endforeach()
    add_custom_target(vulkanxr_sponza_shaders DEPENDS ${SPONZA_SHADER_OUTPUTS})
    cmrc_add_resource_library(vulkanxr_sponza_resources NAMESPACE sponza_lighting_shaders
        WHENCE "${SPONZA_SHADER_BINARY_DIR}" ${SPONZA_SHADER_OUTPUTS})
    add_dependencies(vulkanxr_sponza_resources vulkanxr_sponza_shaders)
    target_link_libraries(vulkanxr_sponza_example PRIVATE vulkanxr_sponza_resources)
    add_executable(vulkanxr_living_room_example
        example/sponza_renderer.cpp
        example/sponza_renderer.hpp
        example/sponza_lighting.hpp
        example/rtx_tv.cpp
        example/rtx_material.cpp
        example/rtx_material.hpp
        example/rtx_neural_cache.cpp
        example/rtx_nrc.cpp
        example/rtx_nrd.cpp
    )
    target_compile_definitions(vulkanxr_living_room_example PRIVATE
        VULKANXR_LIVING_ROOM_EXAMPLE
        VULKANXR_RESOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/resources"
        VULKANXR_CAT_VIDEO="${CMAKE_CURRENT_SOURCE_DIR}/../vulkanizer/test/resources/video/3116737-hd_1920_1080_25fps.mp4")
    target_link_libraries(vulkanxr_living_room_example PRIVATE
        VulkanXR::VulkanXR VulkanXR::shaders spdlog::spdlog
        vulkanxr_nrc vulkanxr_nrd vulkanxr_sponza_resources)
    foreach(runtime IN ITEMS NRC_Vulkan.dll cudart64_13.dll nvrtc64_130_0.dll nvrtc-builtins64_133.dll)
        add_custom_command(TARGET vulkanxr_living_room_example POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${VULKANXR_NRC_SDK_ROOT}/Bin/${runtime}" "$<TARGET_FILE_DIR:vulkanxr_living_room_example>")
    endforeach()
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

    if(VULKANXR_BUILD_EXAMPLES AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/resources/sponza/Sponza.vkmesh")
        add_test(NAME vulkanxr.sponza_ddgi COMMAND vulkanxr_sponza_example
            --desktop --verify-lighting --frames 30)
        add_test(NAME vulkanxr.sponza_nrd COMMAND vulkanxr_sponza_example
            --desktop --nrd --verify-lighting --frames 40)
        add_test(NAME vulkanxr.sponza_nrc_nrd COMMAND vulkanxr_sponza_example
            --desktop --nrc --nrd --full-resolution-reflections --verify-lighting --frames 40)
        set_tests_properties(vulkanxr.sponza_ddgi vulkanxr.sponza_nrd vulkanxr.sponza_nrc_nrd PROPERTIES TIMEOUT 60)
    endif()
    if(VULKANXR_BUILD_EXAMPLES AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/resources/living_room/LivingRoom.vkmesh")
        add_test(NAME vulkanxr.living_room_ddgi COMMAND vulkanxr_living_room_example
            --desktop --verify-lighting --frames 40)
        add_test(NAME vulkanxr.living_room_nrc_nrd COMMAND vulkanxr_living_room_example
            --desktop --nrc --nrd --verify-lighting --frames 40)
        set_tests_properties(vulkanxr.living_room_ddgi vulkanxr.living_room_nrc_nrd PROPERTIES TIMEOUT 90)
    endif()
endif()
