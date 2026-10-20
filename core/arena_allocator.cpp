#include "arena_allocator.hpp"
#include <algorithm>
#include <cstdint>

namespace syspulse {

ArenaAllocator::ArenaAllocator(size_t capacity_bytes)
    : capacity_(capacity_bytes), offset_(0), high_water_mark_(0), allocation_count_(0) {
    if (capacity_ > 0) {
        // Allocate raw aligned memory block
        buffer_ = static_cast<std::byte*>(::operator new(capacity_, std::align_val_t{alignof(std::max_align_t)}));
    }
}

ArenaAllocator::~ArenaAllocator() {
    if (buffer_) {
        ::operator delete(buffer_, std::align_val_t{alignof(std::max_align_t)});
        buffer_ = nullptr;
    }
}

ArenaAllocator::ArenaAllocator(ArenaAllocator&& other) noexcept
    : buffer_(other.buffer_),
      capacity_(other.capacity_),
      offset_(other.offset_),
      high_water_mark_(other.high_water_mark_),
      allocation_count_(other.allocation_count_) {
    other.buffer_ = nullptr;
    other.capacity_ = 0;
    other.offset_ = 0;
    other.high_water_mark_ = 0;
    other.allocation_count_ = 0;
}

ArenaAllocator& ArenaAllocator::operator=(ArenaAllocator&& other) noexcept {
    if (this != &other) {
        if (buffer_) {
            ::operator delete(buffer_, std::align_val_t{alignof(std::max_align_t)});
        }
        buffer_ = other.buffer_;
        capacity_ = other.capacity_;
        offset_ = other.offset_;
        high_water_mark_ = other.high_water_mark_;
        allocation_count_ = other.allocation_count_;

        other.buffer_ = nullptr;
        other.capacity_ = 0;
        other.offset_ = 0;
        other.high_water_mark_ = 0;
        other.allocation_count_ = 0;
    }
    return *this;
}

void* ArenaAllocator::allocate(size_t size, size_t alignment) {
    if (size == 0 || !buffer_) {
        return nullptr;
    }

    // Current pointer address
    uintptr_t current_addr = reinterpret_cast<uintptr_t>(buffer_ + offset_);
    // Align address upwards: (addr + align - 1) & ~(align - 1)
    uintptr_t aligned_addr = (current_addr + (alignment - 1)) & ~(static_cast<uintptr_t>(alignment - 1));
    size_t padding = aligned_addr - current_addr;

    if (offset_ + padding + size > capacity_) {
        // Out of memory in arena
        return nullptr;
    }

    offset_ += padding + size;
    allocation_count_++;
    high_water_mark_ = std::max(high_water_mark_, offset_);

    return reinterpret_cast<void*>(aligned_addr);
}

void ArenaAllocator::reset() noexcept {
    high_water_mark_ = std::max(high_water_mark_, offset_);
    offset_ = 0;
    allocation_count_ = 0;
}

ArenaStats ArenaAllocator::stats() const noexcept {
    return ArenaStats{
        offset_,
        capacity_,
        high_water_mark_,
        allocation_count_
    };
}

} // namespace syspulse
