#pragma once

#include <openxr/openxr.h>

#include <stdexcept>
#include <string>
#include <utility>

namespace vxr {

    class retryable_error : public std::runtime_error {
      public:
        using std::runtime_error::runtime_error;
    };

    class fatal_error : public std::runtime_error {
      public:
        using std::runtime_error::runtime_error;
    };

    class openxr_error : public std::runtime_error {
      public:
        openxr_error(std::string operation, XrResult result) : std::runtime_error{std::move(operation) + " failed with XrResult " + std::to_string(result)}, result_{result} {
        }

        [[nodiscard]] XrResult result() const noexcept {
            return result_;
        }

      private:
        XrResult result_;
    };

    [[nodiscard]] inline std::string result_to_string(XrInstance instance, XrResult result) {
        char text[XR_MAX_RESULT_STRING_SIZE]{};
        if (instance != XR_NULL_HANDLE && XR_SUCCEEDED(xrResultToString(instance, result, text))) {
            return text;
        }
        return std::to_string(result);
    }

    [[nodiscard]] constexpr bool is_client_error(XrResult result) noexcept {
        switch (result) {
        case XR_ERROR_VALIDATION_FAILURE:
        case XR_ERROR_FUNCTION_UNSUPPORTED:
        case XR_ERROR_FEATURE_UNSUPPORTED:
        case XR_ERROR_EXTENSION_NOT_PRESENT:
        case XR_ERROR_LIMIT_REACHED:
        case XR_ERROR_SIZE_INSUFFICIENT:
        case XR_ERROR_HANDLE_INVALID:
        case XR_ERROR_SYSTEM_INVALID:
        case XR_ERROR_SESSION_RUNNING:
        case XR_ERROR_SESSION_NOT_RUNNING:
        case XR_ERROR_SESSION_NOT_READY:
        case XR_ERROR_SESSION_NOT_STOPPING:
        case XR_ERROR_TIME_INVALID:
        case XR_ERROR_REFERENCE_SPACE_UNSUPPORTED:
        case XR_ERROR_FILE_ACCESS_ERROR:
        case XR_ERROR_FILE_CONTENTS_INVALID:
        case XR_ERROR_FORM_FACTOR_UNSUPPORTED:
        case XR_ERROR_API_LAYER_NOT_PRESENT:
        case XR_ERROR_CALL_ORDER_INVALID:
        case XR_ERROR_GRAPHICS_DEVICE_INVALID:
        case XR_ERROR_POSE_INVALID:
        case XR_ERROR_INDEX_OUT_OF_RANGE:
        case XR_ERROR_VIEW_CONFIGURATION_TYPE_UNSUPPORTED:
        case XR_ERROR_ENVIRONMENT_BLEND_MODE_UNSUPPORTED:
        case XR_ERROR_NAME_DUPLICATED:
        case XR_ERROR_NAME_INVALID:
        case XR_ERROR_ACTIONSET_NOT_ATTACHED:
        case XR_ERROR_ACTIONSETS_ALREADY_ATTACHED:
        case XR_ERROR_LOCALIZED_NAME_DUPLICATED:
        case XR_ERROR_LOCALIZED_NAME_INVALID:
        case XR_ERROR_GRAPHICS_REQUIREMENTS_CALL_MISSING:
        case XR_ERROR_PATH_INVALID:
        case XR_ERROR_PATH_COUNT_EXCEEDED:
        case XR_ERROR_PATH_FORMAT_INVALID:
        case XR_ERROR_PATH_UNSUPPORTED:
        case XR_ERROR_LAYER_INVALID:
        case XR_ERROR_LAYER_LIMIT_EXCEEDED:
        case XR_ERROR_SWAPCHAIN_RECT_INVALID:
        case XR_ERROR_SWAPCHAIN_FORMAT_UNSUPPORTED:
        case XR_ERROR_ACTION_TYPE_MISMATCH:
            return true;
        default:
            return false;
        }
    }

    inline void check(XrResult result, const std::string &operation) {
        if (XR_FAILED(result)) {
            throw openxr_error{operation, result};
        }
    }

    inline void check(XrInstance instance, XrResult result, const std::string &operation) {
        if (XR_FAILED(result)) {
            throw openxr_error{operation + ": " + result_to_string(instance, result), result};
        }
    }

} // namespace vxr
