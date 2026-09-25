#pragma once
#include <cstdint>
#include <atomic>

namespace hl::core {

inline uint32_t get_fast_thread_id() noexcept {
    thread_local const uint32_t id = []() noexcept {
        static std::atomic<uint32_t> counter{1};
        return counter.fetch_add(1, std::memory_order_relaxed);
    }();
    return id;
}

} // namespace hl::core
