# Hybrid Lifetimes (`hl`)

[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![Sanitizers](https://img.shields.io/badge/Sanitizers-ASan%20%7C%20TSan%20%7C%20UBSan%20PASS-brightgreen.svg)](#verification--sanitizers)

**`hl`** (Hybrid Lifetimes) is a header-only, C++20 standard-compliant memory library implementing **Fallback Deterministic Reference Counting**. It brings runtime Escape Analysis to C++, offering zero-cost, zero-allocation stack semantics by default, while transparently "hoisting" payloads to heap storage if and only if their lifetime escapes the originating thread or stack frame.

---

## Technical Motivation

Standard C++ forces a rigid choice between two memory lifetime models:

1. **Stack Allocation (`T obj`):** Zero allocation overhead and cache-friendly, but hard-bound to its lexical scope. Passing handles across thread boundaries requires manual lifetime synchronization or risks Use-After-Free (UAF) undefined behavior.
2. **Heap Reference Counting (`std::shared_ptr<T>`):** Safe, dynamic lifetime management across thread boundaries, but enforces mandatory upfront heap allocations (`malloc`/`free`) and atomic control block updates—even for the ~90% of objects that never actually leave local scope.

`hl` eliminates this forced trade-off. Objects are constructed natively on the stack. Lightweight, thread-safe `hl::borrow<T>` handles can be issued down call stacks or dispatched across thread boundaries. If all thread references drop before the originating stack frame exits, the payload is destroyed on the stack with **zero dynamic memory allocations**. If a background thread outlives the original stack frame, `hl` intercepts stack teardown and **transparently hoists** the payload to heap storage without invalidating outstanding handle references.

---

## Architecture & Lifecycles

```text
  [ Local Stack Scope ]                          [ Thread Escape Event ]
  +-------------------+                          +-------------------+
  | hl::local<T>      |                          | hl::borrow<T>     |
  |  +--------------+ |                          |  +--------------+ |
  |  | Inline Storage| |                          |  | Control Block| |
  |  | [ Payload T ]| |                          |  | ref_count: 2 | |
  |  +--------------+ |                          |  +--------------+ |
  +---------+---------+                          +---------+---------+
            |                                              |
            +-------------------+--------------------------+
                                |
                   (Stack Frame Exits Before Thread)
                                |
                                v
               [ Automatic Heap Hoisting Transition ]
               +----------------------------------+
               | Heap Payload: new T(move(stack))|
               | active_ptr -> [ Heap Payload ]   |
               | is_hoisted -> true               |
               +----------------------------------+
```
### Key Innovations (i hope)

1. **Lazy Control Block Instantiation:** Initializing `hl::local<T>` performs zero heap allocations and touches no atomics. A lightweight control block is lazily minted from a thread-local pool only when `.borrow_handle()` is explicitly invoked.
2. **Lock-Free State Transition Engine:** Thread-safe transitions between stack and heap states are managed via an atomic reader registration protocol (`active_readers`), avoiding OS kernel mutexes (`pthread_rwlock_t`).
3. **Thread-Aware Lock Elision:** Dereferencing a `borrow<T>` handle on the originating thread (`creator_thread_id`) completely bypasses reader-writer synchronization barriers, executing near direct pointer speed via CPU instruction-level fast paths.
4. **Leak-Free Thread-Local Caching:** Recycles Control Block metadata via thread-local slab pools wrapped in RAII containers (`CachePool`) to eliminate heap lock contention on frequent borrowing while guaranteeing zero memory leaks on thread termination.

---

## Performance Profile

Benchmarks measured on GCC 13+ (`-O3 -march=native`), 1,000,000 iterations:

| Workload Scenario | `std::shared_ptr<T>` | `hl::local<T>` | Performance Delta | Architectural Reason |
| :--- | :--- | :--- | :--- | :--- |
| **Pure Local Scope Creation** | `7.77 ms` | `4.42 ms` | **1.75x FASTER** | Complete elimination of global `malloc`/`free` calls. |
| **Local Borrows (Micro-bench)**| `15.30 ms` | `21.63 ms` | **0.70x SLOWER** | Payloads require state-check branching (~6ns tax). |
| **Real-World Workload** | `45.58 ms` | `46.10 ms` | **0.99x (PARITY)**| Processing latency hides the 6ns state-check tax. |
| **Deep Call Stack** | `5.58 ms` | `12.67 ms` | **0.44x SLOWER** | Atomic metadata updates during copy pass-by-value. |
| **Thread Escape (Hoisting)** | `141.26 ms` | `133.34 ms` | **1.06x FASTER** | Hoisting hides heap allocation behind thread creation. |

---

## Quickstart & Usage

### 1. Basic Local Usage (Zero Allocation)

```cpp
#include <hl/core/local.hpp>
#include <iostream>

struct Device {
    std::string id;
    void ping() const { std::cout << "Ping: " << id << "\n"; }
};

void process_locally() {
    // Zero dynamic allocation; resides on the current stack frame.
    hl::core::local<Device> dev("dev_01");
    
    dev->ping(); 
    // Out of scope: Object destroyed on stack with 0 ns allocator overhead.
}
```

### 2. Thread Escape & Transparent Hoisting

```cpp
#include <hl/core/local.hpp>
#include <thread>
#include <chrono>

void worker(hl::core::borrow<Device> dev_handle) {
    // Thread-safe access via proxy.
    // If dev_handle points to a hoisted object, access is synchronized transparently.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    dev_handle->ping(); 
}

void dispatch_task() {
    hl::core::local<Device> dev("dev_async");

    // Issue a borrow handle and pass to an asynchronous thread
    std::thread t(worker, dev.borrow_handle());
    t.detach();

    // Function exits HERE. Stack frame collapses.
    // `hl::local` detects active borrow handles in another thread, moves `Device`
    // to the heap, updates pointers lock-free, and exits safely.
}
```

## API Reference

### `hl::core::local<T>`

Primary stack anchor holding inline aligned storage for `T`. Non-copyable, non-movable.

* `template <typename... Args> explicit local(Args&&... args)` — Constructs `T` in-place within stack storage.
* `[[nodiscard]] borrow<T> borrow_handle()` — Lazily initializes the control block and returns a `borrow` reference handle.
* `T* get() noexcept`, `T* operator->() noexcept`, `T& operator*() noexcept` — Direct pointer access to stack payload.

### `hl::core::borrow<T>`

Copyable and movable reference handle tracking the payload (stack or hoisted heap).

* `AccessProxy operator->() const` — Dereferences payload. Returns a thread-safe proxy that locks or bypasses barriers based on lock-elision state.
* `bool is_valid() const noexcept` — Checks if handle points to an active control block.
* `bool is_hoisted() const noexcept` — Returns `true` if the underlying object has migrated to the heap.

---

## Verification & Sanitizers

The codebase is hardened against memory leaks, race conditions, and undefined behavior across multiple LLVM/GCC sanitizer profiles:

```bash
# Build and run verification suite under AddressSanitizer & UndefinedBehaviorSanitizer
make asan

# Build and run verification suite under ThreadSanitizer
make tsan

# Build and run optimized performance benchmark suite (-O3 -march=native)
make bench
```

## License

copyright mxreal64, 2026
This project is licensed under the **MIT License** — see the [LICENSE](LICENSE) file for details.
