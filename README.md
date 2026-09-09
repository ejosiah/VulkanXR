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

## Desktop mode

`vxr::desktop_application` runs a renderer without creating an OpenXR instance. It creates a regular Vulkan context, desktop-owned images exposed through synthetic `vxr::swapchain` handles, and sends the renderer's composition layers to the desktop mirror.

Use `vxr::application_builder` to select the runtime without constructing either application directly. It contains and forwards context configuration to `vxr::context::builder`, and adds `.session(...)`, `.renderer(...)`, and `.mode(...)` configuration. The mode can be `vxr::application_mode::vr` or `vxr::application_mode::desktop`, and the window title is taken from `.app_name(...)`.

The movement example can be launched in desktop mode with:

```powershell
cmake-build-debug\Debug\vulkanxr_movement_example.exe --desktop
```

The desktop input adapter maps the left controller axes to `W`, `S`, `A`, and `D`, and the right controller axes to the arrow keys. `J`, `K`, `U`, and `I` represent the `A`, `B`, `X`, and `Y` buttons; Enter is Select, Escape is Menu, Shift is Squeeze, and Control is the thumbstick or trackpad click. Left and right triggers use `F` and `/` respectively.

## TODO

- Handle `XrEventDataReferenceSpaceChangePending`: identify the affected reference-space type and retain the event until `changeTime`. Existing `XrSpace` handles remain valid and must not be recreated. When `poseValid` is `XR_TRUE`, use `poseInPreviousSpace` to preserve continuity for world-locked content; otherwise reset or re-anchor content whose previous-to-new origin relationship cannot be determined.
