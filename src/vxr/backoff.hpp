#pragma once

#include <cstdint>
#include <string>

namespace vxr {

    class backoff {
      public:
        explicit backoff(std::string message, std::uint32_t max_retries = 5);

        void operator()();

        void reset() noexcept;

      private:
        std::string message_;
        std::uint32_t max_retries_;
        std::uint32_t retries_{};
    };

} // namespace vxr
