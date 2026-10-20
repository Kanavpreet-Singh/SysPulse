#pragma once

#include <atomic>
#include <cstddef>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

namespace syspulse {

// Platform cache line size assumption (64 bytes standard for x86_64 and arm64).
// std::hardware_destructive_interference_size is conditionally supported across compilers,
// so a constant 64-byte alignment ensures portability and prevents false sharing.
inline constexpr size_t CACHE_LINE_SIZE = 64;

/**
 * Lock-free Single-Producer Single-Consumer (SPSC) Ring Buffer.
 * 
 * Requirements & Properties:
 * 1. Power-of-two capacity for fast bitwise masking.
 * 2. Cache-line alignment (alignas(64)) separating head (write) and tail (read)
 *    variables across distinct cache lines to prevent cache line bouncing / false sharing.
 * 3. Formal std::memory_order acquire/release ordering:
 *    - Producer publishes items with std::memory_order_release on write_idx_.
 *    - Consumer observes published items with std::memory_order_acquire on write_idx_.
 *    - Consumer publishes consumed slots with std::memory_order_release on read_idx_.
 *    - Producer observes consumed slots with std::memory_order_acquire on read_idx_.
 */
template <typename T>
class SpscRingBuffer {
public:
    static_assert(std::is_nothrow_destructible_v<T>, "T must be nothrow destructible");

    explicit SpscRingBuffer(size_t requested_capacity) {
        // Enforce power-of-two capacity
        size_t cap = 2;
        while (cap < requested_capacity) {
            cap <<= 1;
        }
        capacity_ = cap;
        mask_ = cap - 1;

        // Allocate raw uninitialized storage for T items to avoid default construction
        storage_ = static_cast<Slot*>(::operator new[](sizeof(Slot) * capacity_, std::align_val_t{alignof(Slot)}));
        write_idx_.store(0, std::memory_order_relaxed);
        read_idx_.store(0, std::memory_order_relaxed);
        cached_read_idx_ = 0;
        cached_write_idx_ = 0;
    }

    ~SpscRingBuffer() {
        // Destroy remaining elements
        T item;
        while (try_pop(item)) {
            // drained
        }
        ::operator delete[](storage_, std::align_val_t{alignof(Slot)});
    }

    SpscRingBuffer(const SpscRingBuffer&) = delete;
    SpscRingBuffer& operator=(const SpscRingBuffer&) = delete;
    SpscRingBuffer(SpscRingBuffer&&) = delete;
    SpscRingBuffer& operator=(SpscRingBuffer&&) = delete;

    [[nodiscard]] size_t capacity() const noexcept {
        return capacity_;
    }

    [[nodiscard]] bool empty() const noexcept {
        // Consumer perspective
        return read_idx_.load(std::memory_order_relaxed) == write_idx_.load(std::memory_order_acquire);
    }

    [[nodiscard]] bool full() const noexcept {
        // Producer perspective
        size_t w = write_idx_.load(std::memory_order_relaxed);
        size_t r = read_idx_.load(std::memory_order_acquire);
        return (w - r) >= capacity_;
    }

    [[nodiscard]] size_t size() const noexcept {
        size_t w = write_idx_.load(std::memory_order_acquire);
        size_t r = read_idx_.load(std::memory_order_acquire);
        return (w >= r) ? (w - r) : 0;
    }

    /**
     * Producer: Attempt to push an item by move.
     * Returns true on success, false if the buffer is full.
     */
    bool try_push(T&& item) {
        const size_t curr_write = write_idx_.load(std::memory_order_relaxed);

        // Check against locally cached read pointer first to avoid cross-thread cache-line loads
        if ((curr_write - cached_read_idx_) >= capacity_) {
            // Update cached read pointer using acquire order to synchronize with consumer's release
            cached_read_idx_ = read_idx_.load(std::memory_order_acquire);
            if ((curr_write - cached_read_idx_) >= capacity_) {
                return false; // Buffer is completely full
            }
        }

        // Construct item in place at the slot
        size_t slot_idx = curr_write & mask_;
        new (&storage_[slot_idx].storage) T(std::move(item));

        // Release ordering: Ensures the constructed object in storage_ is visible
        // to any thread that subsequently acquires write_idx_.
        write_idx_.store(curr_write + 1, std::memory_order_release);
        return true;
    }

    bool try_push(const T& item) {
        T copy = item;
        return try_push(std::move(copy));
    }

    /**
     * Consumer: Attempt to pop an item into out_item.
     * Returns true on success, false if the buffer is empty.
     */
    bool try_pop(T& out_item) {
        const size_t curr_read = read_idx_.load(std::memory_order_relaxed);

        // Check against locally cached write pointer first
        if (curr_read == cached_write_idx_) {
            // Acquire ordering: Ensures all writes performed by the producer
            // prior to releasing write_idx_ are visible before we read the slot.
            cached_write_idx_ = write_idx_.load(std::memory_order_acquire);
            if (curr_read == cached_write_idx_) {
                return false; // Buffer is completely empty
            }
        }

        size_t slot_idx = curr_read & mask_;
        T* slot_ptr = reinterpret_cast<T*>(&storage_[slot_idx].storage);
        out_item = std::move(*slot_ptr);
        slot_ptr->~T();

        // Release ordering: Ensures the destruction/read of the element at slot_idx
        // completes before the producer can observe this slot as free.
        read_idx_.store(curr_read + 1, std::memory_order_release);
        return true;
    }

private:
    struct Slot {
        alignas(alignof(T)) std::byte storage[sizeof(T)];
    };

    size_t capacity_{0};
    size_t mask_{0};
    Slot* storage_{nullptr};

    // Producer state placed on its own cache line
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> write_idx_{0};
    size_t cached_read_idx_{0};

    // Consumer state placed on its own cache line
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> read_idx_{0};
    size_t cached_write_idx_{0};
};

} // namespace syspulse
