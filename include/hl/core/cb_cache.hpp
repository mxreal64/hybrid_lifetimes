#pragma once
#include <vector>
#include <thread>
#include <atomic>
#include "thread_id.hpp"

namespace hl::core {

    template<typename CB>
    struct ThreadLocalCBCache {
        static constexpr size_t MAX_CACHE_SIZE = 1024;

        // RAII wrapper to ensure cached pointers are deleted on thread destruction
        struct CachePool {
            std::vector<CB*> items;
            ~CachePool() {
                for (CB* cb : items) {
                    delete cb;
                }
            }
        };

        static thread_local CachePool cache;

        static CB* allocate() {
            uint32_t tid = get_fast_thread_id();
            if (!cache.items.empty()) {
                CB* cb = cache.items.back();
                cache.items.pop_back();
                cb->ref_count.store(1, std::memory_order_relaxed);
                cb->active_readers.store(0, std::memory_order_relaxed);
                cb->is_hoisted.store(false, std::memory_order_relaxed);
                cb->creator_thread_id = tid;
                return cb;
            }
            CB* cb = new CB();
            cb->creator_thread_id = tid;
            return cb;
        }

        static void deallocate(CB* cb) {
            if (cb->creator_thread_id == get_fast_thread_id() && cache.items.size() < MAX_CACHE_SIZE) {
                cache.items.push_back(cb);
            } else {
                delete cb;
            }
        }
    };

    template<typename CB>
    thread_local typename ThreadLocalCBCache<CB>::CachePool ThreadLocalCBCache<CB>::cache;

} // namespace hl::core
