#pragma once

#include <concepts>
#include <functional>
#include <list>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

template <class K, class Hash, class Eq = std::equal_to<K>>
concept HashableKey =
    std::regular<K> &&
    std::predicate<Eq, const K&, const K&> &&
    requires(Hash h, const K& k) {
        { h(k) } -> std::convertible_to<std::size_t>;
    };

template <class Val>
concept StorableValue = std::movable<Val>;

template <class K, class Val,
          class Hash = std::hash<K>,
          class Eq   = std::equal_to<K>>
struct Shard {
    using List = std::list<std::pair<K, Val>>;
    using It   = typename List::iterator;

    std::mutex mtx;
    std::unordered_map<K, It, Hash, Eq> map; // key -> iterator into lru
    List lru;                                 // front = MRU, back = LRU
    std::size_t capacity = 0;
};

template<class K, class V, std::size_t Capacity,
         class Hash = std::hash<K>, class Eq = std::equal_to<K>>
requires HashableKey<K, Hash, Eq> && StorableValue<V>
class LRUCache {
public:
    static_assert(Capacity > 0);

    LRUCache() {
        // shard.resize(1);
        shard.capacity = Capacity;
    }

    bool get(const K& k, V& out) {
        auto& s = shard;
        // std::scoped_lock lock(s.mtx); // enable when you care about threads

        auto it = s.map.find(k);
        if (it == s.map.end()) { return false; }

        // move node to front (MRU)
        s.lru.splice(s.lru.begin(), s.lru, it->second);

        out = it->second->second; // value
        return true;
    }

    // Returns false if key already exists
    bool put(const K& k, V v) {
        auto& s = shard;
        // std::scoped_lock lock(s.mtx);

        if (auto it = s.map.find(k); it != s.map.end()) {
            return false;
        }

        s.lru.emplace_front(k, std::move(v));
        s.map.emplace(s.lru.front().first, s.lru.begin());

        // evict if over capacity
        if (s.map.size() > s.capacity) {
            const K& victim_key = s.lru.back().first;
            s.map.erase(victim_key);
            s.lru.pop_back();
        }
        return true;
    }

    bool contains(const K& k) const {
        auto& s = shard;
        // std::scoped_lock lock(s.mtx);
        return s.map.find(k) != s.map.end();
    }

    std::size_t size() const {
        auto& s = shard;
        // std::scoped_lock lock(s.mtx);
        return s.map.size();
    }

    void erase(const K& k) {
        auto& s = shard;
        // std::scoped_lock lock(s.mtx);

        auto it = s.map.find(k);
        if (it == s.map.end()) { return; }
        s.lru.erase(it->second);
        s.map.erase(it);
    }

    void clear() {
        auto& s = shard;
        // std::scoped_lock lock(s.mtx);
        s.map.clear();
        s.lru.clear();
    }

private:
    Shard<K, V, Hash, Eq> shard;
};
