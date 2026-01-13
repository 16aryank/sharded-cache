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
    std::cout << "...Passed\n";
}

TEST(CacheTest, HandlesMoveableKey) {

}