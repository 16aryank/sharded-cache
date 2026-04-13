#include "../src/cache.h"
#include "gtest/gtest.h"
#include <string>
#include <random>
#include <set>

TEST(CacheTest, HandlesHashableKey) {
    // Hashable key works
    {
        LRUCache<int, int, 2> cache;
        EXPECT_TRUE(cache.put(1, 100));
        EXPECT_TRUE(cache.put(2, 200));

        int out;
        EXPECT_TRUE(cache.get(1, out));
        EXPECT_EQ(out, 100);
    }

    // Key with custom hash function works
    {
        struct MyKey {
            int x;
            bool operator==(const MyKey& other) const {
                return x == other.x;
            }
        };
        struct MyKeyHash {
            std::size_t operator()(const MyKey& k) const noexcept {
                return std::hash<int>{}(k.x);
            }
        };

        LRUCache<MyKey, int, 2, 1, MyKeyHash> cache;

        EXPECT_TRUE(cache.put({1}, 111));
        EXPECT_TRUE(cache.put({2}, 222));

        int out;
        EXPECT_TRUE(cache.get({1}, out));
        EXPECT_EQ(out, 111);
    }
}

TEST(CacheTest, HandlesMoveableValues) {
    struct NonMovableClass {
        NonMovableClass& operator=(NonMovableClass&&) = delete;
        int x;
    };
    struct MovableClass {
        int x;
    };
    static_assert(!StorableValue<NonMovableClass>);
    static_assert(StorableValue<MovableClass>);
    EXPECT_TRUE(true);
}

TEST(CacheTest, HandlesEviction) {
    LRUCache<int, int, 5, 1> cache;
    int out = 0;
    
    // cache is at capacity
    for (std::size_t i = 0; i < 5; i++) {
        EXPECT_TRUE(cache.put(i, i * 100));
        EXPECT_FALSE(cache.put(i, i * 100));
        EXPECT_TRUE(cache.get(i, out));
    }

    for (std::size_t i = 0; i < 5; i++) {
        EXPECT_TRUE(cache.contains(0));
    }

    // evict Key 0 
    EXPECT_TRUE(cache.put(6, 600));
    EXPECT_FALSE(cache.get(0, out));
}

TEST(CacheTest, HandlesRandom) {
    LRUCache<int, int, 100, 5> cache;
    EXPECT_NO_THROW(cache.erase(0));
    cache.put(0, 0);

    std::mt19937 engine(12345); // fixed seed
    std::uniform_int_distribution<int> dist(1, 10000000);
    std::set<size_t> shards;

    while(cache.size_shard(0) < cache.get_capacity()) {
        const auto& rand = dist(engine);
        cache.put(rand, 0);
        shards.insert(std::hash<int>{}(rand) % 5); // defaults to std::hash
    }
    
    // test consistent hashing
    EXPECT_TRUE(shards.size() == 5);

    // test get/put
    int out = -1;
    EXPECT_TRUE(cache.contains(0));
    EXPECT_TRUE(cache.get(0, out));
    EXPECT_TRUE(out == 0);
    
    EXPECT_FALSE(cache.put(0, -1));
    EXPECT_TRUE(cache.get(0, out));
    EXPECT_TRUE(out == -1);

    // test put updates LRU
    bool found = false;
    int found_value = -1;
    while (!found) {
        const auto& rand = dist(engine);
        if ((std::hash<int>{}(rand) % 5) == 0) {
            found = true;
            found_value = rand;
        }
    }
    cache.put(found_value, 0);
    EXPECT_TRUE(cache.contains(0));

    // test erase
    cache.erase(0);
    EXPECT_FALSE(cache.contains(0));
}

TEST(CacheTest, HandlesClear) {
    LRUCache<int, int, 2, 2> cache;
    bool found = false;
    int zero_val; 
    int one_val;

    std::mt19937 engine(12345); // fixed seed
    std::uniform_int_distribution<int> dist(1, 10000000);

    // append to shard 0
    while (!found) {
        const auto& rand = dist(engine);
        if (std::hash<int>{}(rand) % 2 == 0) {
            found = true;
            cache.put(rand, 0);
            zero_val = rand;
        }
    }

    // append to shard 1
    found = false;
    while (!found) {
        const auto& rand = dist(engine);
        if (std::hash<int>{}(rand) % 2 == 1) {
            found = true;
            cache.put(rand, 0);
            one_val = rand;
        }
    }

    cache.clear(one_val);
    EXPECT_TRUE(cache.size_shard(one_val) == 0);
    EXPECT_TRUE(cache.size_shard(zero_val) == 1);
}