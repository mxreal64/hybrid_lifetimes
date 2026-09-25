#include <iostream>
#include <chrono>
#include <vector>
#include <thread>
#include <memory>
#include <iomanip>
#include <cmath>
#include "../include/hl/core/local.hpp"

using namespace hl::core;

// A realistic payload that does actual work
struct HeavyPayload {
    double data[32];
    HeavyPayload() {
        for (int i = 0; i < 32; ++i) data[i] = i * 1.5;
    }

    // Simulates a real-world method (e.g. math, string processing)
    double process() {
        double sum = 0;
        for (int i = 0; i < 32; ++i) {
            sum += std::sqrt(data[i]);
        }
        return sum;
    }
};

struct LightPayload {
    int value = 42;
    int process() { return value; }
};

template <typename Func>
double measure_ms(Func&& func) {
    auto start = std::chrono::high_resolution_clock::now();
    func();
    auto end = std::chrono::high_resolution_clock::now();
    return std::chrono::duration<double, std::milli>(end - start).count();
}

void print_result(const std::string& name, double sp_ms, double hl_ms) {
    double ratio = sp_ms / hl_ms;
    std::cout << std::left << std::setw(30) << name
    << std::setw(15) << sp_ms
    << std::setw(15) << hl_ms
    << std::setw(10) << ratio
    << (ratio > 1.0 ? " x FASTER (HL Wins)" : " x SLOWER") << "\n";
}

// Helper for deep call stack test
void process_deep_sp(std::shared_ptr<LightPayload> p, int depth, int& result) {
    if (depth == 0) { result += p->process(); return; }
    process_deep_sp(p, depth - 1, result);
}
void process_deep_hl(borrow<LightPayload> p, int depth, int& result) {
    if (depth == 0) { result += p->process(); return; }
    process_deep_hl(p, depth - 1, result);
}

int main() {
    constexpr int ITERS = 1'000'000;

    std::cout << "=== Hybrid Lifetimes (HL) vs std::shared_ptr ===\n";
    std::cout << "Iterations: " << ITERS << "\n\n";
    std::cout << std::left << std::setw(30) << "Benchmark"
    << std::setw(15) << "shared_ptr(ms)"
    << std::setw(15) << "hl::local(ms)"
    << "Ratio\n";
    std::cout << std::string(85, '-') << "\n";

    // 1. Pure Local (No Escapes)
    double pure_sp = measure_ms([&] {
        int sum = 0;
        for (int i = 0; i < ITERS; ++i) {
            auto ptr = std::make_shared<LightPayload>();
            sum += ptr->process();
        }
        volatile int s = sum; (void)s;
    });
    double pure_hl = measure_ms([&] {
        int sum = 0;
        for (int i = 0; i < ITERS; ++i) {
            local<LightPayload> ptr;
            sum += ptr->process();
        }
        volatile int s = sum; (void)s;
    });
    print_result("1. Pure Local (Creation)", pure_sp, pure_hl);

    // 2. Local Borrows (Empty Payload)
    double borrow_sp = measure_ms([&] {
        int sum = 0;
        for (int i = 0; i < ITERS; ++i) {
            auto ptr = std::make_shared<LightPayload>();
            auto b = ptr;
            sum += b->process();
        }
        volatile int s = sum; (void)s;
    });
    double borrow_hl = measure_ms([&] {
        int sum = 0;
        for (int i = 0; i < ITERS; ++i) {
            local<LightPayload> ptr;
            auto b = ptr.borrow_handle();
            sum += b->process();
        }
        volatile int s = sum; (void)s;
    });
    print_result("2. Local Borrows (Micro)", borrow_sp, borrow_hl);

    // 3. Real-World Workload (Proves overhead vanishes)
    double real_sp = measure_ms([&] {
        double sum = 0;
        for (int i = 0; i < ITERS; ++i) {
            auto ptr = std::make_shared<HeavyPayload>();
            auto b = ptr;
            sum += b->process();
        }
        volatile double s = sum; (void)s;
    });
    double real_hl = measure_ms([&] {
        double sum = 0;
        for (int i = 0; i < ITERS; ++i) {
            local<HeavyPayload> ptr;
            auto b = ptr.borrow_handle();
            sum += b->process();
        }
        volatile double s = sum; (void)s;
    });
    print_result("3. Real-World Workload", real_sp, real_hl);

    // 4. Deep Call Stack (Passing by value 10 levels deep)
    double deep_sp = measure_ms([&] {
        int sum = 0;
        for (int i = 0; i < ITERS/10; ++i) {
            auto ptr = std::make_shared<LightPayload>();
            process_deep_sp(ptr, 10, sum);
        }
        volatile int s = sum; (void)s;
    });
    double deep_hl = measure_ms([&] {
        int sum = 0;
        for (int i = 0; i < ITERS/10; ++i) {
            local<LightPayload> ptr;
            process_deep_hl(ptr.borrow_handle(), 10, sum);
        }
        volatile int s = sum; (void)s;
    });
    print_result("4. Deep Call Stack", deep_sp, deep_hl);

    // 5. Thread Escapes (Hoisting)
    double hoist_sp = measure_ms([&] {
        for (int i = 0; i < ITERS/100; ++i) {
            auto ptr = std::make_shared<LightPayload>();
            std::thread t([p = ptr]() { volatile int v = p->process(); (void)v; });
            t.join();
        }
    });
    double hoist_hl = measure_ms([&] {
        for (int i = 0; i < ITERS/100; ++i) {
            local<LightPayload> ptr;
            std::thread t([b = ptr.borrow_handle()]() { volatile int v = b->process(); (void)v; });
            t.join();
        }
    });
    print_result("5. Thread Escapes (Hoist)", hoist_sp, hoist_hl);

    return 0;
}
