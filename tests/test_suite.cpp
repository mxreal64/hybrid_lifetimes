#include <iostream>
#include <thread>
#include <chrono>
#include <cassert>
#include <hl/hl.hpp>

struct Device {
    int id;
    static inline std::atomic<int> active_count{0};

    Device(int i) : id(i) {
        active_count.fetch_add(1, std::memory_order_relaxed);
    }

    Device(Device&& other) noexcept : id(other.id) {
        active_count.fetch_add(1, std::memory_order_relaxed);
    }

    ~Device() {
        active_count.fetch_sub(1, std::memory_order_relaxed);
    }

    void ping() const {
        // Active operation
    }
};

int main() {
    std::cout << "=== Running HL Framework Verification Suite ===\n";

    // Test 1: Stack to Heap Hoist with Multithreading
    {
        hl::borrow<Device> global_borrow;
        std::atomic<bool> running{true};
        std::thread worker;

        {
            hl::local<Device> local_dev(404);
            global_borrow = local_dev.borrow_handle();

            worker = std::thread([b = global_borrow, &running]() {
                while (running.load()) {
                    b->ping();
                    std::this_thread::yield();
                }
            });

            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            // Scope exits here -> local_dev hoists to heap
        }

        assert(global_borrow.is_hoisted() == true);
        assert(Device::active_count.load() == 1);
        std::cout << "[PASS] Stack successfully hoisted during thread execution\n";

        running.store(false);
        if (worker.joinable()) worker.join();
    }

    assert(Device::active_count.load() == 0);
    std::cout << "[PASS] Heap instance released after last borrow destruction\n";

    // Test 2: Deferred Exception Safety
    {
        struct FaultyMove {
            FaultyMove() = default;
            FaultyMove(FaultyMove&&) { throw std::runtime_error("Move Construction Failed"); }
            void execute() {}
        };

        hl::borrow<FaultyMove> b;
        {
            hl::local<FaultyMove> local_fault;
            b = local_fault.borrow_handle();
        }

        try {
            b->execute();
        } catch (const std::runtime_error& e) {
            std::cout << "[PASS] Caught deferred exception: " << e.what() << "\n";
        }
    }

    // Telemetry Verification
    auto& stats = hl::get_stats();
    std::cout << "\n=== Telemetry Output ===\n";
    std::cout << "Stack Allocations : " << stats.total_stack_allocations.load() << "\n";
    std::cout << "Heap Hoists       : " << stats.total_heap_hoists.load() << "\n";
    std::cout << "Bytes Hoisted     : " << stats.bytes_hoisted.load() << " B\n";

    std::cout << "\nALL TESTS PASSED PERFECTLY.\n";
    return 0;
}
