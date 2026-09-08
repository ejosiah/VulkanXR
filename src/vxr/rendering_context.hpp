#pragma once

#include <vulkanizer/context.hpp>

namespace vxr {

    class rendering_context {
      public:
        virtual ~rendering_context() = default;

        [[nodiscard]] virtual vkz::context &vulkan_context() noexcept = 0;

        [[nodiscard]] virtual const vkz::context &vulkan_context() const noexcept = 0;

        [[nodiscard]] virtual std::uint32_t graphics_queue_family() const noexcept = 0;

        [[nodiscard]] virtual VkQueue graphics_queue() const noexcept = 0;

        [[nodiscard]] vkz::device &device() noexcept {
            return vulkan_context().device;
        }

        [[nodiscard]] const vkz::device &device() const noexcept {
            return vulkan_context().device;
        }

        [[nodiscard]] VkInstance vulkan_instance() const noexcept {
            return vulkan_context().instance;
        }
    };

} // namespace vxr
