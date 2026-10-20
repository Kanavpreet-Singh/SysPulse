#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <utility>

namespace syspulse {

struct ArenaStats {
    size_t bytes_allocated{0};
    size_t bytes_capacity{0};
    size_t high_water_mark{0};
    size_t allocation_count{0};
};

/**
 * Linear Bump Arena Allocator.
 * 
 * Preallocates a contiguous block of memory to serve allocations
 * during a sampling cycle without calling heap malloc/free.
 * Resets in O(1) time at the end of each sampling cycle.
 */
class ArenaAllocator {
public:
    explicit ArenaAllocator(size_t capacity_bytes);
    ~ArenaAllocator();

    ArenaAllocator(const ArenaAllocator&) = delete;
    ArenaAllocator& operator=(const ArenaAllocator&) = delete;
    ArenaAllocator(ArenaAllocator&& other) noexcept;
    ArenaAllocator& operator=(ArenaAllocator&& other) noexcept;

    /**
     * Allocate aligned memory from the arena.
     * Default alignment matches std::max_align_t.
     */
    [[nodiscard]] void* allocate(size_t size, size_t alignment = alignof(std::max_align_t));

    /**
     * Allocate and construct an object of type T in the arena.
     */
    template <typename T, typename... Args>
    [[nodiscard]] T* create(Args&&... args) {
        void* mem = allocate(sizeof(T), alignof(T));
        if (!mem) {
            return nullptr;
        }
        return new (mem) T(std::forward<Args>(args)...);
    }

    /**
     * Resets the bump pointer to zero.
     * Updates high-water mark before reset. Does not invoke destructors
     * (used for POD / trivially destructible telemetry structures).
     */
    void reset() noexcept;

    [[nodiscard]] ArenaStats stats() const noexcept;
    [[nodiscard]] size_t bytes_used() const noexcept { return offset_; }
    [[nodiscard]] size_t capacity() const noexcept { return capacity_; }
    [[nodiscard]] size_t high_water_mark() const noexcept { return high_water_mark_; }
    [[nodiscard]] size_t allocation_count() const noexcept { return allocation_count_; }

private:
    std::byte* buffer_{nullptr};
    size_t capacity_{0};
    size_t offset_{0};
    size_t high_water_mark_{0};
    size_t allocation_count_{0};
};

} // namespace syspulse
