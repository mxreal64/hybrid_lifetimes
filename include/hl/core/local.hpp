#pragma once
#include "control_block.hpp"
#include "borrow.hpp"
#include "cb_cache.hpp"
#include "../trace/telemetry.hpp"
#include <utility>
#include <new>

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#endif

namespace hl::core {

    template <Hoistable T>
    class local {
        using CB = ControlBlock<T>;

        alignas(T) std::byte storage_[sizeof(T)];
        CB* cb_ = nullptr;
        bool initialized_ = false;

        T* stack_ptr() noexcept { return reinterpret_cast<T*>(storage_); }

    public:
        template <typename... Args>
        explicit local(Args&&... args) {
            new (stack_ptr()) T(std::forward<Args>(args)...);
            initialized_ = true;
            trace::record_stack_alloc();
        }

        local(const local&) = delete;
        local& operator=(const local&) = delete;
        local(local&&) = delete;

        ~local() noexcept {
            if (!initialized_) return;

            if (!cb_) {
                stack_ptr()->~T();
                return;
            }

            if (cb_->ref_count.fetch_sub(1, std::memory_order_acq_rel) > 1) {

                T* heap_obj = nullptr;
                try {
                    heap_obj = new T(std::move(*stack_ptr()));
                    trace::record_hoist(sizeof(T));
                } catch (...) {
                    stack_ptr()->~T();
                    cb_->active_ptr.store(nullptr, std::memory_order_release);
                    cb_->is_hoisted.store(true, std::memory_order_release);
                    return;
                }

                cb_->active_ptr.store(heap_obj, std::memory_order_release);
                cb_->is_hoisted.store(true, std::memory_order_release);

                // Lock-Free Drain: Wait for in-flight cross-thread readers to finish
                while (cb_->active_readers.load(std::memory_order_acquire) > 0) {
                    #if defined(__x86_64__) || defined(_M_X64)
                    _mm_pause();
                    #endif
                }

                stack_ptr()->~T();
            } else {
                stack_ptr()->~T();
                ThreadLocalCBCache<CB>::deallocate(cb_);
            }
        }

        [[nodiscard]] borrow<T> borrow_handle() {
            if (!cb_) {
                cb_ = ThreadLocalCBCache<CB>::allocate();
                cb_->active_ptr.store(stack_ptr(), std::memory_order_relaxed);
                cb_->ref_count.store(2, std::memory_order_relaxed);
            } else {
                cb_->ref_count.fetch_add(1, std::memory_order_relaxed);
            }
            return borrow<T>(cb_);
        }

        T* get() noexcept { return stack_ptr(); }
        T* operator->() noexcept { return stack_ptr(); }
        T& operator*() noexcept { return *stack_ptr(); }
    };

} // namespace hl::core
