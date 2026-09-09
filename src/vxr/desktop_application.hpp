#pragma once

#include <vxr/application.hpp>
#include <vxr/desktop_input.hpp>

#include <vulkanizer/memory.hpp>

#include <map>

namespace vxr {

    class desktop_application final : public app {
      public:
        desktop_application(session::config session_config, renderer_factory renderer_factory, mirror_config mirror_config = {});

        void run() override;

        void shutdown() noexcept override;

      private:
        void create_vulkan_context();

        void create_rendering_context();

        void create_swapchains();

        void create_renderer();

        void process_frame();

        void cleanup() noexcept;

        session::config session_config_;
        renderer_factory renderer_factory_;
        mirror_config mirror_config_;
        vkz::glfw_runtime windowing_;
        window_ptr window_;
        surface_provider_ptr surface_provider_;
        vkz::context vulkan_context_{};
        std::unique_ptr<rendering::context> context_;
        vkz::vma_memory_allocator allocator_{};
        std::map<std::string, std::vector<vkz::image>> images_;
        std::unique_ptr<renderer> renderer_;
        std::unique_ptr<mirror> mirror_;
        std::unique_ptr<desktop_input_adaptor> input_;
        std::map<XrSwapchain, std::uint32_t> image_indices_;
        bool shutdown_requested_{};
    };

} // namespace vxr
