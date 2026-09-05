#include <vxr/swapchain.hpp>

#include <vxr/openxr_helpers.hpp>
#include <vxr/status.hpp>

#include <utility>

namespace vxr {

    swapchain::swapchain(XrSession session, swapchain_create_info create_info) : create_info_{std::move(create_info)} {
        auto xr_create_info = make_struct<XrSwapchainCreateInfo>();
        xr_create_info.createFlags = create_info_.create_flags_;
        xr_create_info.usageFlags = create_info_.usage_flags_;
        xr_create_info.format = static_cast<std::int64_t>(create_info_.format_);
        xr_create_info.sampleCount = create_info_.sample_count_;
        xr_create_info.width = create_info_.width_;
        xr_create_info.height = create_info_.height_;
        xr_create_info.faceCount = create_info_.face_count_;
        xr_create_info.arraySize = create_info_.array_size_;
        xr_create_info.mipCount = create_info_.mip_count_;

        check(xrCreateSwapchain(session, &xr_create_info, &handle_), "xrCreateSwapchain");

        try {
            std::uint32_t image_count{};
            check(xrEnumerateSwapchainImages(handle_, 0, &image_count, nullptr), "xrEnumerateSwapchainImages count");

            images_.assign(image_count, make_struct<XrSwapchainImageVulkanKHR>());
            check(xrEnumerateSwapchainImages(handle_, image_count, &image_count, reinterpret_cast<XrSwapchainImageBaseHeader *>(images_.data())), "xrEnumerateSwapchainImages");
            images_.resize(image_count);
        } catch (...) {
            destroy();
            throw;
        }
    }

    void swapchain::destroy() noexcept {
        if (handle_ != XR_NULL_HANDLE) {
            xrDestroySwapchain(handle_);
        }
        handle_ = XR_NULL_HANDLE;
        images_.clear();
    }

} // namespace vxr
