#pragma once
#include <atomic>
#include <cstdint>

namespace hl::trace {

struct TelemetryStats {
    std::atomic<uint64_t> total_stack_allocations{0};
    std::atomic<uint64_t> total_heap_hoists{0};
    std::atomic<uint64_t> bytes_hoisted{0};
};

inline TelemetryStats& get_stats() noexcept {
    static TelemetryStats stats;
    return stats;
}

inline void record_stack_alloc() noexcept {
    get_stats().total_stack_allocations.fetch_add(1, std::memory_order_relaxed);
}

inline void record_hoist(size_t bytes) noexcept {
    get_stats().total_heap_hoists.fetch_add(1, std::memory_order_relaxed);
    get_stats().bytes_hoisted.fetch_add(bytes, std::memory_order_relaxed);
}

} // namespace hl::trace
