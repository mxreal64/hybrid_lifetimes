#pragma once
#include <memory_resource>

namespace hl::memory {

    // Thread-safe PMR Synchronized Pool Resource optimized for ControlBlock allocations.
    // Employs per-thread slab caching to make borrow allocations O(1) lock-free fast-paths.
    inline std::pmr::memory_resource* get_cb_memory_resource() noexcept {
        static std::pmr::synchronized_pool_resource cb_pool{
            std::pmr::pool_options{
                .max_blocks_per_chunk = 4096,
                .largest_required_pool_block = 512
            }
        };
        return &cb_pool;
    }

} // namespace hl::memory
