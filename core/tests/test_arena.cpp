#include <gtest/gtest.h>
#include "arena_allocator.hpp"
#include <cstdint>

using namespace syspulse;

TEST(ArenaTest, BasicAllocationAndAlignment) {
    ArenaAllocator arena(1024);
    EXPECT_EQ(arena.capacity(), 1024);
    EXPECT_EQ(arena.bytes_used(), 0);

    void* ptr1 = arena.allocate(17, 8);
    ASSERT_NE(ptr1, nullptr);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(ptr1) % 8, 0);

    void* ptr2 = arena.allocate(32, 64);
    ASSERT_NE(ptr2, nullptr);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(ptr2) % 64, 0);

    EXPECT_GT(arena.bytes_used(), 17 + 32);
    EXPECT_EQ(arena.allocation_count(), 2);
}

TEST(ArenaTest, Exhaustion) {
    ArenaAllocator arena(128);
    void* p1 = arena.allocate(100);
    ASSERT_NE(p1, nullptr);

    // This should fail as 100 + 50 > 128
    void* p2 = arena.allocate(50);
    EXPECT_EQ(p2, nullptr);

    // Stats should show 1 successful allocation
    EXPECT_EQ(arena.allocation_count(), 1);
}

TEST(ArenaTest, ResetAndHighWaterMark) {
    ArenaAllocator arena(512);

    void* p1 = arena.allocate(200);
    ASSERT_NE(p1, nullptr);
    size_t mark1 = arena.high_water_mark();
    EXPECT_GE(mark1, 200);

    arena.reset();
    EXPECT_EQ(arena.bytes_used(), 0);
    EXPECT_EQ(arena.allocation_count(), 0);
    EXPECT_EQ(arena.high_water_mark(), mark1);

    // Reallocate less than mark1
    void* p2 = arena.allocate(100);
    ASSERT_NE(p2, nullptr);
    EXPECT_EQ(arena.high_water_mark(), mark1); // should remain at peak

    // Allocate more to exceed peak
    void* p3 = arena.allocate(250);
    ASSERT_NE(p3, nullptr);
    EXPECT_GT(arena.high_water_mark(), mark1);
}

struct TestStruct {
    int a;
    double b;
    TestStruct(int x, double y) : a(x), b(y) {}
};

TEST(ArenaTest, CreateHelper) {
    ArenaAllocator arena(256);
    TestStruct* obj = arena.create<TestStruct>(42, 3.1415);
    ASSERT_NE(obj, nullptr);
    EXPECT_EQ(obj->a, 42);
    EXPECT_DOUBLE_EQ(obj->b, 3.1415);
}
