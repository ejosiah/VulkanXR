#pragma once

#include <vxr/action.hpp>
#include <vxr/context.hpp>
#include <vxr/models.hpp>
#include <vxr/session_context.hpp>
#include <vxr/transforms.hpp>

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace vxr {

    class renderer {
      public:
        renderer(context &xr_context, session::context &session_context) : xr_context_{xr_context}, session_context_{session_context} {
        }

        virtual ~renderer() = default;

        virtual void begin_frame() {
        }

        virtual void end_frame() {
        }

        virtual void set(const std::vector<space_location> &space_locations) {
        }

        virtual std::vector<vibration> set(const action_set &actions) {
            return {};
        }

        virtual void init() {
        }

        virtual void cleanup() {
        }

        [[nodiscard]] virtual const char *name() const noexcept {
            return "";
        }

        virtual void paused(const frame_info &frame, layers &composition_layers) = 0;

        virtual void render(const frame_info &frame, layers &composition_layers) = 0;

        [[nodiscard]] virtual std::string active_action_sets() const {
            return "*";
        }

      protected:
        [[nodiscard]] vkz::device &device() const noexcept {
            return xr_context_.device();
        }

        [[nodiscard]] vkz::context &vulkan_context() const noexcept {
            return xr_context_.vulkan_context();
        }

        [[nodiscard]] std::uint32_t graphics_queue_family() const noexcept {
            return xr_context_.graphics_queue_family();
        }

        [[nodiscard]] VkQueue graphics_queue() const noexcept {
            return xr_context_.graphics_queue();
        }

        [[nodiscard]] swapchain &get_swapchain(const std::string &name) {
            return session_context_.swapchains.at(name);
        }

        [[nodiscard]] swapchain &get_swapchain(XrSwapchain handle) {
            const auto found = std::ranges::find_if(session_context_.swapchains, [handle](const auto &entry) {
                return entry.second.handle() == handle;
            });
            if (found == session_context_.swapchains.end()) {
                throw std::out_of_range{"OpenXR swapchain handle was not found in the session context"};
            }
            return found->second;
        }

        context &xr_context_;
        session::context &session_context_;
    };

} // namespace vxr
