#include <gtest/gtest.h>
#include "spsc_ring_buffer.hpp"
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

using namespace syspulse;

TEST(RingBufferTest, BasicPushPop) {
    SpscRingBuffer<int> ring(4);
    EXPECT_GE(ring.capacity(), 4);
    EXPECT_TRUE(ring.empty());
    EXPECT_FALSE(ring.full());

    EXPECT_TRUE(ring.try_push(10));
    EXPECT_TRUE(ring.try_push(20));
    EXPECT_FALSE(ring.empty());

    int val = 0;
    EXPECT_TRUE(ring.try_pop(val));
    EXPECT_EQ(val, 10);
    EXPECT_TRUE(ring.try_pop(val));
    EXPECT_EQ(val, 20);
    EXPECT_TRUE(ring.empty());
}

TEST(RingBufferTest, FullAndEmpty) {
    SpscRingBuffer<int> ring(4);
    size_t cap = ring.capacity();

    for (size_t i = 0; i < cap; ++i) {
        EXPECT_TRUE(ring.try_push(static_cast<int>(i)));
    }

    EXPECT_TRUE(ring.full());
    EXPECT_FALSE(ring.try_push(999)); // Cannot push when full

    int val = 0;
    for (size_t i = 0; i < cap; ++i) {
        EXPECT_TRUE(ring.try_pop(val));
        EXPECT_EQ(val, static_cast<int>(i));
    }

    EXPECT_TRUE(ring.empty());
    EXPECT_FALSE(ring.try_pop(val)); // Cannot pop when empty
}

TEST(RingBufferTest, Wraparound) {
    SpscRingBuffer<int> ring(8);
    // Push and pop across multiple wraparounds of the underlying buffer
    for (int cycle = 0; cycle < 100; ++cycle) {
        for (int i = 0; i < 5; ++i) {
            EXPECT_TRUE(ring.try_push(cycle * 10 + i));
        }
        for (int i = 0; i < 5; ++i) {
            int val = 0;
            EXPECT_TRUE(ring.try_pop(val));
            EXPECT_EQ(val, cycle * 10 + i);
        }
    }
    EXPECT_TRUE(ring.empty());
}

TEST(RingBufferTest, TwoThreadStress10M) {
    constexpr size_t TOTAL_ITEMS = 10'000'000;
    constexpr size_t BUFFER_CAPACITY = 65536;

    SpscRingBuffer<uint64_t> ring(BUFFER_CAPACITY);
    std::atomic<bool> producer_done{false};

    auto start_time = std::chrono::steady_clock::now();

    // Consumer thread: verifies strict monotonicity and total count
    std::thread consumer([&]() {
        uint64_t expected = 0;
        uint64_t val = 0;
        size_t consumed_count = 0;

        while (consumed_count < TOTAL_ITEMS) {
            if (ring.try_pop(val)) {
                if (val != expected) {
                    FAIL() << "Order violation: expected " << expected << ", got " << val;
                }
                expected++;
                consumed_count++;
            } else if (producer_done.load(std::memory_order_acquire) && ring.empty()) {
                break;
            } else {
                std::this_thread::yield();
            }
        }
        EXPECT_EQ(consumed_count, TOTAL_ITEMS);
    });

    // Producer thread: pushes 0 to TOTAL_ITEMS - 1
    std::thread producer([&]() {
        for (uint64_t i = 0; i < TOTAL_ITEMS; ++i) {
            while (!ring.try_push(i)) {
                std::this_thread::yield();
            }
        }
        producer_done.store(true, std::memory_order_release);
    });

    producer.join();
    consumer.join();

    auto end_time = std::chrono::steady_clock::now();
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
    EXPECT_GT(elapsed_ms, 0);
}
