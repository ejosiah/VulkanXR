# Reuse the SDK installations used by the sibling light_transport reference.
# These cache paths can point at independent SDK installations/builds.
set(VULKANXR_NRC_SDK_ROOT "${PROJECT_SOURCE_DIR}/../light_transport/_deps/nrc-sdk" CACHE PATH "NVIDIA NRC SDK root")
set(VULKANXR_NRD_SDK_ROOT "${PROJECT_SOURCE_DIR}/../light_transport/_deps/nrd-sdk" CACHE PATH "NVIDIA NRD SDK root")
set(VULKANXR_NRD_LIBRARY "${PROJECT_SOURCE_DIR}/../light_transport/cmake-build-debug/_deps/nrd-build/Debug/NRD.lib" CACHE FILEPATH "NRD static library with embedded SPIR-V, normal encoding 2, roughness encoding 1")

set(VULKANXR_NRD_BLOB_LIBRARY "${PROJECT_SOURCE_DIR}/../light_transport/cmake-build-debug/_deps/shadermake-build/Debug/ShaderMakeBlob.lib" CACHE FILEPATH "NRD ShaderMake blob library")

foreach(required IN ITEMS "${VULKANXR_NRC_SDK_ROOT}/Include/NrcVk.h" "${VULKANXR_NRC_SDK_ROOT}/Lib/NRC_Vulkan.lib"
        "${VULKANXR_NRD_SDK_ROOT}/Include/NRD.h" "${VULKANXR_NRD_LIBRARY}" "${VULKANXR_NRD_BLOB_LIBRARY}")
    if(NOT EXISTS "${required}")
        message(FATAL_ERROR "Sponza SDK dependency missing: ${required}. Set VULKANXR_NRC_SDK_ROOT, VULKANXR_NRD_SDK_ROOT and VULKANXR_NRD_LIBRARY.")
    endif()
endforeach()

add_library(vulkanxr_nrc SHARED IMPORTED)
set_target_properties(vulkanxr_nrc PROPERTIES
    IMPORTED_IMPLIB "${VULKANXR_NRC_SDK_ROOT}/Lib/NRC_Vulkan.lib"
    IMPORTED_LOCATION "${VULKANXR_NRC_SDK_ROOT}/Bin/NRC_Vulkan.dll"
    INTERFACE_INCLUDE_DIRECTORIES "${VULKANXR_NRC_SDK_ROOT}/Include")
add_library(vulkanxr_nrd STATIC IMPORTED)
set_target_properties(vulkanxr_nrd PROPERTIES
    IMPORTED_LOCATION "${VULKANXR_NRD_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${VULKANXR_NRD_SDK_ROOT}/Include"
    INTERFACE_LINK_LIBRARIES "${VULKANXR_NRD_BLOB_LIBRARY}")
find_program(VULKANXR_DXC dxc HINTS "$ENV{VULKAN_SDK}/Bin" REQUIRED)
