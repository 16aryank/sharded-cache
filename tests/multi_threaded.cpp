#include "../src/cache.h"
#include "gtest/gtest.h"
#include <atomic>
#include <barrier>
#include <chrono>
#include <future>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

TEST(MultiThreadedCacheTest, ConcurrentOpsSameShard) {
    LRUCache<int, int, 64, 1> cache; // single shard
    std::atomic<bool> start{false};
    std::vector<std::thread> threads;

    threads.emplace_back([&] {
        while (!start.load()) {}
        for (int i = 0; i < 1000; ++i) {
            cache.put(i % 128, i);
        }
    });

    threads.emplace_back([&] {
        while (!start.load()) {}
        int out = 0;
        for (int i = 0; i < 1000; ++i) {
            cache.get(i % 128, out);
        }
    });

    threads.emplace_back([&] {
        while (!start.load()) {}
        for (int i = 0; i < 1000; ++i) {
            cache.contains(i % 128);
        }
    });

    threads.emplace_back([&] {
        while (!start.load()) {}
        for (int i = 0; i < 200; ++i) {
            cache.erase(i % 128);
        }
    });

    threads.emplace_back([&] {
        while (!start.load()) {}
        for (int i = 0; i < 50; ++i) {
            cache.clear_shard(i);
        }
    });

    threads.emplace_back([&] {
        while (!start.load()) {}
        for (int i = 0; i < 10; ++i) {
            cache.clear_all();
        }
    });

    start.store(true);
    for (auto& t : threads) {
        t.join();
    }

    EXPECT_LE(cache.size_shard(0), cache.capacity_per_shard());
}

TEST(MultiThreadedCacheTest, ConcurrentGetAndEraseLinearizable) {
    LRUCache<int, int, 8, 1> cache;
    const int key = 42;

    for (int i = 0; i < 200; ++i) {
        cache.put(key, 1);
        std::barrier sync(2);

        bool got = false;
        int out = 0;

        std::thread t1([&] {
            sync.arrive_and_wait();
            got = cache.get(key, out);
        });

        std::thread t2([&] {
            sync.arrive_and_wait();
            cache.erase(key);
        });

        t1.join();
        t2.join();

        if (got) {
            EXPECT_EQ(out, 1);
        }
        EXPECT_FALSE(cache.contains(key));
    }
}

TEST(MultiThreadedCacheTest, GetOrComputeReturnsFutureAndHandlesExceptions) {
    LRUCache<int, int, 4, 1> cache;
    const int key = 7;

    std::atomic<int> compute_count{0};
    std::promise<void> gate;
    std::shared_future<void> gate_fut = gate.get_future().share();
    std::promise<void> started;
    std::shared_future<void> started_fut = started.get_future().share();

    std::shared_future<int> producer_fut;
    std::thread producer([&] {
        producer_fut = cache.get_or_compute(key, [&] {
            compute_count.fetch_add(1);
            started.set_value();
            gate_fut.wait();
            return 123;
        });
    });

    started_fut.wait();

    auto start = std::chrono::steady_clock::now();
    auto waiter_fut = cache.get_or_compute(key, [&] {
        compute_count.fetch_add(1);
        return 999;
    });
    auto elapsed = std::chrono::steady_clock::now() - start;
    EXPECT_LT(elapsed, 20ms);
    EXPECT_FALSE(cache.contains(key));

    gate.set_value();
    producer.join();
    EXPECT_EQ(waiter_fut.get(), 123);
    EXPECT_EQ(producer_fut.get(), 123);
    EXPECT_TRUE(cache.contains(key));
    EXPECT_EQ(compute_count.load(), 1);

    std::atomic<int> exc_count{0};
    std::promise<void> exc_started;
    std::shared_future<void> exc_started_fut = exc_started.get_future().share();
    std::shared_future<int> fut_exc;

    std::thread exc_producer([&] {
        try {
            fut_exc = cache.get_or_compute(key + 1, [&] {
                exc_count.fetch_add(1);
                exc_started.set_value();
                throw std::runtime_error("boom");
                return 0;
            });
        } catch (const std::runtime_error&) {
        }
    });

    exc_started_fut.wait();
    auto waiter_exc_fut = cache.get_or_compute(key + 1, [&] {
        exc_count.fetch_add(1);
        return 111;
    });

    EXPECT_THROW(waiter_exc_fut.get(), std::runtime_error);
    exc_producer.join();
    EXPECT_FALSE(cache.contains(key + 1));

    auto fut_recover = cache.get_or_compute(key + 1, [&] {
        exc_count.fetch_add(1);
        return 456;
    });
    EXPECT_EQ(fut_recover.get(), 456);
    EXPECT_EQ(exc_count.load(), 2);
}

TEST(MultiThreadedCacheTest, GetOrComputeSingleProducerMultipleWaiters) {
    LRUCache<int, int, 8, 1> cache;
    const int key = 99;
    std::atomic<int> compute_count{0};
    std::promise<void> start_compute;
    std::shared_future<void> start_fut = start_compute.get_future().share();

    std::barrier sync(2);
    std::shared_future<int> fut1;
    std::shared_future<int> fut2;

    std::thread t1([&] {
        sync.arrive_and_wait();
        fut1 = cache.get_or_compute(key, [&] {
            compute_count.fetch_add(1);
            start_fut.wait();
            return 777;
        });
    });

    std::thread t2([&] {
        sync.arrive_and_wait();
        fut2 = cache.get_or_compute(key, [&] {
            compute_count.fetch_add(1);
            return 888;
        });
    });

    start_compute.set_value();
    t1.join();
    t2.join();

    EXPECT_EQ(compute_count.load(), 1);
    const int v1 = fut1.get();
    const int v2 = fut2.get();
    EXPECT_EQ(v1, v2);
    EXPECT_TRUE(v1 == 777 || v1 == 888);
    EXPECT_TRUE(cache.contains(key));
}

TEST(MultiThreadedCacheTest, ContainsFalseDuringInflightTrueAfter) {
    LRUCache<int, int, 8, 1> cache;
    const int key = 5;

    std::promise<void> gate;
    std::shared_future<void> gate_fut = gate.get_future().share();
    std::promise<void> started;
    std::shared_future<void> started_fut = started.get_future().share();

    std::shared_future<int> fut;
    std::thread producer([&] {
        fut = cache.get_or_compute(key, [&] {
            started.set_value();
            gate_fut.wait();
            return 321;
        });
    });

    started_fut.wait();
    EXPECT_FALSE(cache.contains(key));
    gate.set_value();
    producer.join();
    EXPECT_EQ(fut.get(), 321);
    EXPECT_TRUE(cache.contains(key));
}
