#pragma once
#include <atomic>

// Architecture-specific pause instruction to prevent CPU pipeline flushes during spins
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__)
#include <immintrin.h>
inline void cpu_pause() noexcept { _mm_pause(); }
#elif defined(__aarch64__) || defined(__arm__)
inline void cpu_pause() noexcept { asm volatile("yield" ::: "memory"); }
#else
inline void cpu_pause() noexcept {}
#endif

namespace hl::core {

    class RWSpinLock {
        std::atomic<uint32_t> state_{0};
        static constexpr uint32_t WRITER_MASK = 1U << 31;

    public:
        RWSpinLock() = default;

        void lock_shared() noexcept {
            while (true) {
                // fast-path: Single hardware instruction (lock xadd)
                uint32_t s = state_.fetch_add(1, std::memory_order_acquire);

                if (s & WRITER_MASK) {
                    // Writer is active, back off immediately
                    state_.fetch_sub(1, std::memory_order_release);

                    // Spin using hardware pause to save power and pipeline
                    while (state_.load(std::memory_order_relaxed) & WRITER_MASK) {
                        cpu_pause();
                    }
                } else {
                    return; // Acquired!
                }
            }
        }

        void unlock_shared() noexcept {
            state_.fetch_sub(1, std::memory_order_release);
        }

        void lock() noexcept {
            uint32_t s = state_.load(std::memory_order_relaxed);
            while (true) {
                if (s & WRITER_MASK) {
                    s = state_.load(std::memory_order_relaxed);
                    cpu_pause();
                    continue;
                }
                if (state_.compare_exchange_weak(s, s | WRITER_MASK,
                    std::memory_order_acquire,
                    std::memory_order_relaxed)) {
                    break;
                    }
            }

            // Wait for existing readers to drain
            while ((state_.load(std::memory_order_acquire) & ~WRITER_MASK) != 0) {
                cpu_pause();
            }
        }

        void unlock() noexcept {
            state_.fetch_and(~WRITER_MASK, std::memory_order_release);
        }
    };

} // namespace hl::core
