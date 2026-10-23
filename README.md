# VulkanXR

VulkanXR hides the complexity of building an OpenXR application with Vulkan and gives you a clear, expressive C++20 DSL instead. You describe the application, session, actions, swapchains, renderer, desktop mirror, and execution mode; VulkanXR handles the OpenXR and Vulkan plumbing needed to run them together.

That includes OpenXR instance and system creation, Vulkan graphics requirements, session state transitions, reference spaces, action synchronization, swapchain acquisition, frame timing, composition layers, and orderly shutdown. Renderers receive the Vulkan and session state they need without having to own the OpenXR application lifecycle.

You do not need a headset connected—or even an OpenXR runtime running—to work on your renderer. Desktop mode skips OpenXR context creation, creates a regular Vulkan context, supplies desktop-backed equivalents of the swapchains and frame state expected by the renderer, adapts keyboard and mouse input to actions and poses, and presents the returned composition layers in a desktop window. This makes normal rendering work, shader iteration, validation, and debugging available throughout development. When you are ready to test in a headset, switch the application mode back to VR; the renderer, session configuration, actions, and composition-layer output remain the same.

The result is application setup that reads like configuration rather than initialization machinery:

```cpp
#include <vxr/vxr.hpp>

vxr::session::config session_config() {
    vxr::session::config config;
    config
        .stage()
        .add_action_set(
            vxr::session::action_set_specification{}
                .name("movement")
                .description("Movement controls")
                .add_action("move_x", vxr::input::oculus_touch::layout::left_thumbstick_x, "Strafe left and right")
                .add_action("move_y", vxr::input::oculus_touch::layout::left_thumbstick_y, "Move forward and backward")
        )
        .add_swapchain(
            vxr::swapchain_create_info{}
                .name("projection")
                .usage()
                    .color_attachment()
                .format(VK_FORMAT_R8G8B8A8_SRGB)
                .extent(2064, 2096)
                .array_size(2)
        );
    return config;
}

int main(int argc, char **argv) {
    auto application =
        vxr::application_builder{}
            .app_name("My VulkanXR App")
            .vulkan_api_version(VK_API_VERSION_1_2)
            .openxr_api_version(XR_API_VERSION_1_0)
            .mirror()
                .extent(1280, 720)
                .multiview(true)
                .vsync(false)
            .session(session_config())
            .renderer(my_renderer::create)
            .mode(argc > 1 && std::string_view{argv[1]} == "--desktop" ? vxr::application_mode::desktop : vxr::application_mode::vr)
        .build();

    application->run();
}
```

Your renderer focuses on rendering and returns standard OpenXR composition layers. VulkanXR coordinates those layers with the runtime in VR mode or displays them through the Vulkan desktop mirror in desktop mode, allowing the same application to be developed on a monitor and then run in a headset.

The project uses Conan 2 and CMake, following the same dependency layout as Vista.

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

This mode is intended for day-to-day development when a headset is unavailable, disconnected, or inconvenient to wear. No OpenXR runtime or active VR session is required. Renderer initialization, swapchain rendering, composition-layer construction, camera movement, input handling, mirroring, and most graphics debugging can run entirely on the desktop. Hardware-specific behavior—such as runtime capabilities, tracked-device poses, interaction-profile changes, and final headset presentation—can then be verified separately in VR mode without maintaining a second renderer.

Use `vxr::application_builder` to select the runtime without constructing either application directly. It contains and forwards context configuration to `vxr::context::builder`, and adds `.session(...)`, `.renderer(...)`, and `.mode(...)` configuration. The mode can be `vxr::application_mode::vr` or `vxr::application_mode::desktop`, and the window title is taken from `.app_name(...)`.

The movement example can be launched in desktop mode with:

```powershell
cmake-build-debug\Debug\vulkanxr_movement_example.exe --desktop
```

The desktop input adapter maps the left controller axes to `W`, `S`, `A`, and `D`, and the right controller axes to the arrow keys. `J`, `K`, `U`, and `I` represent the `A`, `B`, `X`, and `Y` buttons; Enter is Select, Escape is Menu, Shift is Squeeze, and Control is the thumbstick or trackpad click. Left and right triggers use `F` and `/` respectively.

## Sponza lighting demo

The Sponza demo uses vulkanizer 0.0.49: a raster G-buffer, DDGI diffuse GI,
ray-traced shadows and reflections with the built-in temporal/spatial denoisers,
and the same Uncharted 2 tone mapping as rtx_test. Each eye owns independent
screen-space textures and histories. Scene color/depth rasterization and probe
visualization use 4x MSAA, resolving HDR color before tone mapping. The lighting
G-buffer and screen-space effects remain single-sampled. The point light is at `{0, 1, -2}` with
5000 K color, intensity 10, range 5, and the reference color/intensity scaling.
The projection remains 2064x2096 per eye; GI, shadows and reflections default
to half resolution. It uses Vulkan 1.2 with dynamic rendering and synchronization2 extensions, ray tracing, and ray queries.

```powershell
cmake-build-debug\Debug\vulkanxr_sponza_example.exe --desktop
```

Omit `--desktop` to use OpenXR. `--full-resolution-gi`,
`--full-resolution-shadows`, and `--full-resolution-reflections` independently
select native resolution; `--full-resolution` selects all three.
`--no-gi`, `--no-shadows`, `--no-reflections`, `--no-probe-offsetting`,
`--visualize-probes`, `--indirect-only`, `--reflection-only`, and
`--gi-multiplier <value>` control lighting at startup.
The old full-primary-ray reference renderer is no longer used by this demo.

`--nrc` selects Neural Radiance Cache GI; `--ddgi` selects the default DDGI.
NRC starts bounce rays from each eye's raster G-buffer and uses a separate cache
context for each eye. `--nrd` enables RELAX for diffuse GI and reflections and
SIGMA for shadows; the built-in shadow/reflection denoisers are bypassed.
`--no-nrd` restores the built-in denoisers. NRD histories and output resolutions
remain independent per eye and per effect.

```powershell
cmake-build-debug\Debug\vulkanxr_sponza_example.exe --desktop --nrc --nrd
```

The example's SDK paths default to the sibling light_transport installations.
Override `VULKANXR_NRC_SDK_ROOT`, `VULKANXR_NRD_SDK_ROOT`,
`VULKANXR_NRD_LIBRARY`, and `VULKANXR_NRD_BLOB_LIBRARY` in CMake as needed.
NRD must contain embedded SPIR-V with normal encoding 2 and roughness encoding 1.
The NRC runtime DLLs are copied beside the executable. DXC compiles the NRC
bridge shaders. All shader descriptors remain ordinary private descriptor sets.
The NRC 0.15 SDK reports live SDK/CUDA objects during Vulkan device teardown,
also seen in the reference; these cleanup reports are tracked separately from
rendering synchronization and descriptor validation.


For bounded validation, run `--desktop --validation --verify-lighting --frames 60`.
This checks both eyes' GPU radiance and visibility readbacks before shutting down.

## Living room lighting demo

`vulkanxr_living_room_example` uses the same stereo raster, DDGI/NRC, shadows,
reflections and optional NRD setup. It uses the first two authored point lights
from `resources/living_room/LivingRoom.vkmesh`, retaining their positions,
colors, intensities and ranges. Its DDGI grid matches rtx_test: 7x4x7 probes
covering the scene bounds, offsetting enabled, classification disabled and
self-shadow bias 0.05. The camera starts inside the room facing its center.

```powershell
cmake-build-debug\Debug\vulkanxr_living_room_example.exe --desktop
cmake-build-debug\Debug\vulkanxr_living_room_example.exe --desktop --nrc --nrd
```

Omit `--desktop` for OpenXR. All Sponza lighting options also apply here.
`--living-room` selects this scene in the Sponza executable, and `--sponza`
selects Sponza in the living room executable. The living room mesh, material
and bounds files are copied from vulkanizer's `test/mesh/resources/living_room`.

The living room ceiling lamp retains its authored alpha blend mode, base-color alpha and texture alpha in both VulkanXR and rtx_test. The ceiling point light uses its original source size; the lamp has no emission override. Lighting paths respect alpha coverage for masked/blended materials.

## TODO

- Handle `XrEventDataReferenceSpaceChangePending`: identify the affected reference-space type and retain the event until `changeTime`. Existing `XrSpace` handles remain valid and must not be recreated. When `poseValid` is `XR_TRUE`, use `poseInPreviousSpace` to preserve continuity for world-locked content; otherwise reset or re-anchor content whose previous-to-new origin relationship cannot be determined.

The living room TV plays the cat clip from the sibling Vulkanizer
`test/resources/video` directory. It decodes and filters each presentation frame
once, then shares that frame between both eyes. The screen emits the current
video colors into DDGI/NRC and reflections; textured LTC adds diffuse and glossy
area lighting with scene visibility. The implementation follows the
[original authors' reference](https://eheitzresearch.wordpress.com/415-2/) and
[official LTC tables](https://github.com/selfshadow/ltc_code), with attribution
in `LICENSES/ltc.txt`. Video decoding uses an additional H.264 decode queue in
both desktop and OpenXR device creation.
`--video-path "C:/path/to/video.mp4"` selects the TV video in either lighting executable. Without it, the TV uses the cat clip. Quote paths containing spaces.

TV audio is shared by both eyes and synchronized to the audio sample clock. It starts after the first stereo frame and loops with the video. The default cat clip has a silent audio track; use --video-path with an audible clip for sound.

Living room scene lights can be disabled with --no-lights (both lamps), --no-ceiling-light, or --no-standing-light. These flags affect the authored scene lights; TV video emission and LTC lighting remain enabled. --no-lights also disables Sponza's point light. The cushion fabrics use the ceiling lamp's metallic/roughness factors and texture (effective metalness 0 and roughness 194/255), retaining their own base color and normal maps.
