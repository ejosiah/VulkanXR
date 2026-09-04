# VulkanXR

A minimal C++20 project using the same Conan 2 + CMake setup as Vista.

OpenXR 1.1.62 is consumed from the installed Khronos OpenXR SDK, following the
`virtual_reality` reference project. Set `OPENXR_DIR` to the SDK's `cmake`
directory if CMake cannot discover it automatically.

The repository follows the Pitchfork separation between library sources and
example programs: library code lives in `src/`, while runnable sample code
lives in `example/`. The top-level `openxr_info` subproject uses Vulkanizer
and ImGui to display API layers and instance extensions in separate tabs.

Shader files placed under `shaders/` are embedded with CMRC in the
`VulkanXR::shaders` target. In C++, access them through the generated
`vxr_shaders` resource namespace.

## Configure and build

From a Visual Studio developer PowerShell:

```powershell
conan install . -of cmake-build-debug -s build_type=Debug -s compiler.cppstd=20 --build=missing
cmake --preset conan-default
cmake --build --preset conan-debug
ctest --preset conan-debug
```

For Release, use `cmake-build-release` and `-s build_type=Release`. The generated
Conan presets are included by `CMakeUserPresets.json`, matching Vista's layout.
