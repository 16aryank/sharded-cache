#include "../src/cache.h"
#include "gtest/gtest.h"

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
        LRUCache<MyKey, int, 2, MyKeyHash> cache;

        EXPECT_TRUE(cache.put({1}, 111));
        EXPECT_TRUE(cache.put({2}, 222));

        int out;
        EXPECT_TRUE(cache.get({1}, out));
        EXPECT_EQ(out, 111);
    }
}

TEST(CacheTest, HandlesMoveableValues) {
    // ensure movable values work
}

TEST(CacheTest, HandlesEviction) {
    LRUCache<int, int, 5> cache;
    EXPECT_EQ(cache.size(), std::size_t{0});
    int out = 0;
    
    // cache is at capacity
    for (std::size_t i = 0; i < 5; i++) {
        EXPECT_TRUE(cache.put(i, i * 100));
        EXPECT_FALSE(cache.put(i, i * 100));
        EXPECT_TRUE(cache.get(i, out));
        EXPECT_EQ(cache.size(), i + 1);
    }

    for (std::size_t i = 0; i < 5; i++) {
        EXPECT_TRUE(cache.contains(0));
    }

    // evict Key 0 
    EXPECT_TRUE(cache.put(6, 600));
    EXPECT_FALSE(cache.get(0, out));

    EXPECT_EQ(cache.size(), std::size_t{5});
}

TEST(CacheTest, HandlesBasic) {
    LRUCache<int, int, 0> cache;
    // put when at capacity
    // put when it's already there
    // test erase when empty
    // test erase when full
    // test contains
}