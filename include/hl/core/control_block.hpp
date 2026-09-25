#pragma once
#include <atomic>
#include <cstdint>
#include "thread_id.hpp"

namespace hl::core {

    template <typename T>
    concept Hoistable = std::move_constructible<T> && std::destructible<T>;

    template <typename T>
    struct ControlBlock {
        std::atomic<size_t> ref_count{1};
        std::atomic<uint32_t> active_readers{0}; // Non-blocking lock-free reader count
        std::atomic<T*> active_ptr{nullptr};
        std::atomic<bool> is_hoisted{false};
        uint32_t creator_thread_id{0};

        void destroy_hoisted() noexcept {
            T* ptr = active_ptr.load(std::memory_order_relaxed);
            if (ptr) delete ptr;
        }
    };

} // namespace hl::core
