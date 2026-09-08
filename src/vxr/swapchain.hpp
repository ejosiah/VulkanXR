#pragma once

#include <volk.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace vxr {

    struct swapchain_create_info {
        std::string name_;
        XrSwapchainCreateFlags create_flags_{};
        XrSwapchainUsageFlags usage_flags_{};
        VkFormat format_{VK_FORMAT_UNDEFINED};
        std::uint32_t sample_count_{1};
        std::uint32_t width_{};
        std::uint32_t height_{};
        std::uint32_t face_count_{1};
        std::uint32_t array_size_{1};
        std::uint32_t mip_count_{1};
        bool render_{true};
        std::string depth_;
        std::string required_openxr_extension_;

        swapchain_create_info &name(std::string value) {
            name_ = std::move(value);
            return *this;
        }

        swapchain_create_info &static_image() {
            create_flags_ |= XR_SWAPCHAIN_CREATE_STATIC_IMAGE_BIT;
            return *this;
        }

        swapchain_create_info &add_usage(XrSwapchainUsageFlags value) {
            usage_flags_ |= value;
            return *this;
        }

        swapchain_create_info &usage() {
            return *this;
        }

        swapchain_create_info &color_attachment() {
            return add_usage(XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT);
        }

        swapchain_create_info &depth_stencil_attachment() {
            return add_usage(XR_SWAPCHAIN_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
        }

        swapchain_create_info &transfer_source() {
            return add_usage(XR_SWAPCHAIN_USAGE_TRANSFER_SRC_BIT);
        }

        swapchain_create_info &transfer_destination() {
            return add_usage(XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT);
        }

        swapchain_create_info &sampled() {
            return add_usage(XR_SWAPCHAIN_USAGE_SAMPLED_BIT);
        }

        swapchain_create_info &format(VkFormat value) {
            format_ = value;
            return *this;
        }

        swapchain_create_info &sample_count(std::uint32_t value) {
            sample_count_ = value;
            return *this;
        }

        swapchain_create_info &extent(std::uint32_t value_width, std::uint32_t value_height) {
            width_ = value_width;
            height_ = value_height;
            return *this;
        }

        swapchain_create_info &face_count(std::uint32_t value) {
            face_count_ = value;
            return *this;
        }

        swapchain_create_info &array_size(std::uint32_t value) {
            array_size_ = value;
            return *this;
        }

        swapchain_create_info &mip_count(std::uint32_t value) {
            mip_count_ = value;
            return *this;
        }

        swapchain_create_info &render(bool value) {
            render_ = value;
            return *this;
        }

        swapchain_create_info &depth(std::string value) {
            depth_ = std::move(value);
            return *this;
        }

        swapchain_create_info &required_openxr_extension(std::string value) {
            required_openxr_extension_ = std::move(value);
            return *this;
        }
    };

    class swapchain {
      public:
        swapchain() = default;

        swapchain(XrSession session, swapchain_create_info create_info);

        swapchain(swapchain_create_info create_info, std::span<const VkImage> images);

        void destroy() noexcept;

        [[nodiscard]] XrSwapchain handle() const noexcept {
            return handle_;
        }

        [[nodiscard]] const std::string &name() const noexcept {
            return create_info_.name_;
        }

        [[nodiscard]] VkFormat format() const noexcept {
            return create_info_.format_;
        }

        [[nodiscard]] std::uint32_t width() const noexcept {
            return create_info_.width_;
        }

        [[nodiscard]] std::uint32_t height() const noexcept {
            return create_info_.height_;
        }

        [[nodiscard]] const swapchain_create_info &create_info() const noexcept {
            return create_info_;
        }

        [[nodiscard]] std::span<const XrSwapchainImageVulkanKHR> images() const noexcept {
            return images_;
        }

        [[nodiscard]] explicit operator bool() const noexcept {
            return handle_ != XR_NULL_HANDLE;
        }

      private:
        swapchain_create_info create_info_;
        XrSwapchain handle_{XR_NULL_HANDLE};
        std::vector<XrSwapchainImageVulkanKHR> images_;
        void *desktop_handle_{};
    };

} // namespace vxr
