#pragma once
#include "control_block.hpp"
#include "cb_cache.hpp"
#include "thread_id.hpp"
#include <stdexcept>

namespace hl::core {

    template <Hoistable T>
    class borrow {
        template <Hoistable U> friend class local;

        using CB = ControlBlock<T>;
        CB* cb_ = nullptr;

        explicit borrow(CB* cb) noexcept : cb_(cb) {}

    public:
        class AccessProxy {
            CB* cb_ = nullptr;
            T* ptr_ = nullptr;
            bool registered_reader_ = false;

        public:
            explicit AccessProxy(CB* cb) {
                if (!cb) throw std::runtime_error("Dereferencing null borrow handle");

                // FAST PATH 1: Already hoisted -> Direct lock-free atomic read
                if (cb->is_hoisted.load(std::memory_order_acquire)) {
                    ptr_ = cb->active_ptr.load(std::memory_order_relaxed);
                    if (!ptr_) throw std::runtime_error("Object destroyed during move exception");
                    return;
                }

                // FAST PATH 2: Local Thread Lock Elision
                if (cb->creator_thread_id == get_fast_thread_id()) {
                    ptr_ = cb->active_ptr.load(std::memory_order_relaxed);
                    if (!ptr_) throw std::runtime_error("Object pointer is null");
                    return;
                }

                // SLOW PATH: Lock-free cross-thread reader registration
                cb->active_readers.fetch_add(1, std::memory_order_acquire);
                registered_reader_ = true;
                cb_ = cb;
                ptr_ = cb->active_ptr.load(std::memory_order_acquire);
                if (!ptr_) {
                    cb->active_readers.fetch_sub(1, std::memory_order_release);
                    registered_reader_ = false;
                    throw std::runtime_error("Object pointer is null");
                }
            }

            ~AccessProxy() noexcept {
                if (registered_reader_ && cb_) {
                    cb_->active_readers.fetch_sub(1, std::memory_order_release);
                }
            }

            AccessProxy(const AccessProxy&) = delete;
            AccessProxy& operator=(const AccessProxy&) = delete;

            T* operator->() const noexcept { return ptr_; }
            T& operator*() const noexcept { return *ptr_; }
        };

        borrow() noexcept = default;

        borrow(const borrow& other) noexcept : cb_(other.cb_) {
            if (cb_) cb_->ref_count.fetch_add(1, std::memory_order_relaxed);
        }

        borrow(borrow&& other) noexcept : cb_(other.cb_) {
            other.cb_ = nullptr;
        }

        borrow& operator=(const borrow& other) noexcept {
            if (this != &other) {
                release();
                cb_ = other.cb_;
                if (cb_) cb_->ref_count.fetch_add(1, std::memory_order_relaxed);
            }
            return *this;
        }

        borrow& operator=(borrow&& other) noexcept {
            if (this != &other) {
                release();
                cb_ = other.cb_;
                other.cb_ = nullptr;
            }
            return *this;
        }

        ~borrow() noexcept { release(); }

        void release() noexcept {
            if (cb_) {
                if (cb_->ref_count.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                    if (cb_->is_hoisted.load(std::memory_order_acquire)) {
                        cb_->destroy_hoisted();
                    }
                    ThreadLocalCBCache<CB>::deallocate(cb_);
                }
                cb_ = nullptr;
            }
        }

        [[nodiscard]] AccessProxy operator->() const {
            return AccessProxy(cb_);
        }

        [[nodiscard]] bool is_valid() const noexcept { return cb_ != nullptr; }

        [[nodiscard]] bool is_hoisted() const noexcept {
            return cb_ && cb_->is_hoisted.load(std::memory_order_acquire);
        }
    };

} // namespace hl::core
