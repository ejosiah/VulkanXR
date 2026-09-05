#pragma once

#include <openxr/openxr.h>

#include <stdexcept>
#include <string>
#include <utility>

namespace vxr {

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
