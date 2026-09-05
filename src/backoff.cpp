#include <vxr/backoff.hpp>

#include <spdlog/spdlog.h>

#include <chrono>
#include <cmath>
#include <stdexcept>
#include <thread>
#include <utility>

namespace vxr {

    backoff::backoff(std::string message, std::uint32_t max_retries) : message_{std::move(message)}, max_retries_{max_retries} {
    }

    void backoff::operator()() {
        ++retries_;
        if (retries_ > max_retries_) {
            spdlog::error("{} after {} retries", message_, max_retries_);
            throw std::runtime_error{message_ + " after " + std::to_string(max_retries_) + " retries"};
        }

        const auto sleep_time = static_cast<int>(std::round(std::exp(0.68 * static_cast<double>(retries_))));
        spdlog::info("{}, will try again in {} seconds", message_, sleep_time);
        std::this_thread::sleep_for(std::chrono::seconds{sleep_time});
    }

    void backoff::reset() noexcept {
        retries_ = 0;
    }

} // namespace vxr
