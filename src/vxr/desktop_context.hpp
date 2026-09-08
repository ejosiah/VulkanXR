#pragma once

#include <vxr/rendering_context.hpp>

namespace vxr {

    class desktop_context final : public rendering_context {
      public:
        desktop_context(vkz::context vulkan_context, std::uint32_t graphics_queue_family, VkQueue graphics_queue) noexcept
            : vulkan_context_{std::move(vulkan_context)}, graphics_queue_family_{graphics_queue_family}, graphics_queue_{graphics_queue} {
        }

        [[nodiscard]] vkz::context &vulkan_context() noexcept override {
            return vulkan_context_;
        }

        [[nodiscard]] const vkz::context &vulkan_context() const noexcept override {
            return vulkan_context_;
        }

        [[nodiscard]] std::uint32_t graphics_queue_family() const noexcept override {
            return graphics_queue_family_;
        }

        [[nodiscard]] VkQueue graphics_queue() const noexcept override {
            return graphics_queue_;
        }

      private:
        vkz::context vulkan_context_{};
        std::uint32_t graphics_queue_family_{};
        VkQueue graphics_queue_{VK_NULL_HANDLE};
    };

} // namespace vxr
