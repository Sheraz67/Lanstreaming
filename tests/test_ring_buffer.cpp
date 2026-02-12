#include <gtest/gtest.h>
#include "core/ring_buffer.h"
#include <thread>
#include <vector>
#include <numeric>

using namespace lancast;

TEST(RingBufferTest, BasicPushPop) {
    RingBuffer<int, 4> rb;
    EXPECT_TRUE(rb.empty());
    EXPECT_EQ(rb.size(), 0u);

    EXPECT_TRUE(rb.try_push(42));
    EXPECT_FALSE(rb.empty());
    EXPECT_EQ(rb.size(), 1u);

    auto val = rb.try_pop();
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*val, 42);
    EXPECT_TRUE(rb.empty());
}

TEST(RingBufferTest, FullBuffer) {
    RingBuffer<int, 4> rb; // Capacity 4, but usable slots = 3 (one wasted for full detection)

    EXPECT_TRUE(rb.try_push(1));
    EXPECT_TRUE(rb.try_push(2));
    EXPECT_TRUE(rb.try_push(3));
    EXPECT_FALSE(rb.try_push(4)); // Full

    EXPECT_EQ(rb.size(), 3u);
}

TEST(RingBufferTest, EmptyPop) {
    RingBuffer<int, 4> rb;
    auto val = rb.try_pop();
    EXPECT_FALSE(val.has_value());
}

TEST(RingBufferTest, WrapAround) {
    RingBuffer<int, 4> rb;

    // Fill and drain multiple times to force wrap-around
    for (int round = 0; round < 5; ++round) {
        EXPECT_TRUE(rb.try_push(round * 10 + 1));
        EXPECT_TRUE(rb.try_push(round * 10 + 2));

        auto v1 = rb.try_pop();
        auto v2 = rb.try_pop();
        ASSERT_TRUE(v1.has_value());
        ASSERT_TRUE(v2.has_value());
        EXPECT_EQ(*v1, round * 10 + 1);
        EXPECT_EQ(*v2, round * 10 + 2);
    }
}

TEST(RingBufferTest, MoveSemantics) {
    RingBuffer<std::vector<int>, 4> rb;

    std::vector<int> data = {1, 2, 3, 4, 5};
    EXPECT_TRUE(rb.try_push(std::move(data)));
    EXPECT_TRUE(data.empty()); // Should have been moved

    auto result = rb.try_pop();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->size(), 5u);
    EXPECT_EQ((*result)[0], 1);
}

TEST(RingBufferTest, Capacity) {
    RingBuffer<int, 16> rb;
    EXPECT_EQ(rb.capacity(), 16u);
}

TEST(RingBufferTest, ConcurrentSPSC) {
    RingBuffer<int, 1024> rb;
    constexpr int N = 100000;

    std::thread producer([&] {
        for (int i = 0; i < N; ++i) {
            while (!rb.try_push(std::move(i))) {
                std::this_thread::yield();
            }
        }
    });

    std::vector<int> received;
    received.reserve(N);

    std::thread consumer([&] {
        while (received.size() < N) {
            auto val = rb.try_pop();
            if (val) {
                received.push_back(*val);
            } else {
                std::this_thread::yield();
            }
        }
    });

    producer.join();
    consumer.join();

    ASSERT_EQ(received.size(), N);
    for (int i = 0; i < N; ++i) {
        EXPECT_EQ(received[i], i) << "Mismatch at index " << i;
    }
}
