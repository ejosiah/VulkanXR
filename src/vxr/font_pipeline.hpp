#pragma once

#include <vulkanizer/descriptors.hpp>
#include <vulkanizer/pipeline.hpp>

#include <glm/vec4.hpp>

#include <string>

namespace vxr {

    struct font_push_constants {
        glm::vec4 background_color{0.02F, 0.03F, 0.05F, 1.0F};
    };

    struct font_pipeline_create_info {
        vkz::device device;
        vkz::descriptor_set_layout descriptor_layout;
        VkFormat color_format{};
        VkExtent2D extent{};
        std::string vertex_shader;
        std::string fragment_shader;
        std::string name{"font"};
    };

    class font_pipeline {
      public:
        [[nodiscard]] static font_pipeline create(const font_pipeline_create_info &create_info);

        void destroy() noexcept;

        void bind(VkCommandBuffer command_buffer, vkz::descriptor_set descriptor_set, const font_push_constants &constants);

      private:
        vkz::pipeline pipeline_{};
    };

} // namespace vxr
