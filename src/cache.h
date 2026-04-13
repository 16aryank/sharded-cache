#pragma once

#include "shard.h"
#include <concepts>
#include <functional>
#include <future>
#include <list>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>
#include <stdexcept>
#include <iostream>

template <class K, class Hash = std::hash<K>, class Eq = std::equal_to<K>>
concept HashableKey =
    std::regular<K> &&
    std::predicate<Eq, const K&, const K&> &&
    requires(const Hash& h, const K& k) {
        { h(k) } -> std::convertible_to<std::size_t>;
    };

template <class Val>
concept StorableValue = std::copyable<Val>;

template<class K, class V, std::size_t capacity, std::size_t num_shards = 1,
         class Hash = std::hash<K>, class Eq = std::equal_to<K>>
requires HashableKey<K, Hash, Eq> && StorableValue<V>
class LRUCache {
public:
    
    static_assert(capacity > 0, "Cache capacity must be positive");
    static_assert(num_shards > 0, "Number of shards must be positive");

    LRUCache() : _capacity(capacity) {
        _shards.resize(num_shards);
        for (auto& shard : _shards) {
            shard = std::make_unique<Shard<K, V, Hash, Eq>>();
        }
    }

    bool get(const K& k, V& out) {
        auto& s = *_shards[get_shard(k)];
        std::scoped_lock lock(s._mtx);

        auto it = s._map.find(k);
        if (it == s._map.end()) {
            return false;
        }

        // move node to front (MRU)
        s._lru.splice(s._lru.begin(), s._lru, it->second);

        out = it->second->second; // value
        return true;
    }

    // Returns false if key already exists
    bool put(const K& k, V v) {
        auto& s = *_shards[get_shard(k)];
        std::scoped_lock lock(s._mtx);

        // update pointers
        auto it = s._map.find(k);
        if (it != s._map.end()) {
            s._lru.splice(s._lru.begin(), s._lru, it->second);
            it->second->second = std::move(v);
            return false;
        }

        s._lru.emplace_front(k, std::move(v));
        s._map.emplace(s._lru.front().first, s._lru.begin());

        // evict if over capacity
        if (s._map.size() > _capacity) {
            const K& victim_key = s._lru.back().first;
            s._map.erase(victim_key);
            s._lru.pop_back();
        }
        return true;
    }

    bool contains(const K& k) const {
        auto& s = *_shards[get_shard(k)];
        std::scoped_lock lock(s._mtx);
        return s._map.find(k) != s._map.end();
    }

    void erase(const K& k) {
        auto& s = *_shards[get_shard(k)];
        std::scoped_lock lock(s._mtx);

        auto it = s._map.find(k);
        if (it == s._map.end()) {
            return; 
        }
        s._lru.erase(it->second);
        s._map.erase(it);
    }

    // Clears only the shard that would contain key k.
    void clear_shard(const K& k) {
        auto& s = *_shards[get_shard(k)];
        std::scoped_lock lock(s._mtx);
        s._map.clear();
        s._lru.clear();
    }

    // Backwards-compatible alias for shard-level clear.
    void clear(const K& k) { clear_shard(k); }

    // Clears all shards (entire cache).
    void clear_all() {
        for (auto& shard : _shards) {
            std::scoped_lock lock(shard->_mtx);
            shard->_map.clear();
            shard->_lru.clear();
        }
    }

    // Size of the shard that would contain key k.
    std::size_t size_shard(const K& k) const {
        auto& s = *_shards[get_shard(k)];
        std::scoped_lock lock(s._mtx);
        return s._map.size();
    }

    // Per-shard capacity (not global capacity).
    std::size_t capacity_per_shard() const noexcept {
        return _capacity;
    }

    // Backwards-compatible alias for per-shard capacity.
    std::size_t get_capacity() const noexcept { return capacity_per_shard(); }

    std::size_t shard_count() const noexcept { return _shards.size(); }

    template <class F>
    std::shared_future<V> get_or_compute(const K& key, F&& fn) {
        auto& s = *_shards[get_shard(key)];
        std::shared_future<V> fut;
        std::promise<V> p;

        {
            std::unique_lock<std::mutex> lock(s._mtx);

            auto it = s._map.find(key);
            if (it != s._map.end()) {
                // move node to front (MRU)
                s._lru.splice(s._lru.begin(), s._lru, it->second);
                std::promise<V> ready;
                auto ready_fut = ready.get_future().share();
                ready.set_value(it->second->second);
                return ready_fut;
            }

            auto inflight_it = s.inflight.find(key);
            if (inflight_it != s.inflight.end()) {
                fut = inflight_it->second;
                return fut;
            }

            fut = p.get_future().share();
            s.inflight.emplace(key, fut);
        }

        try {
            V v = fn();

            {
                std::unique_lock<std::mutex> lock(s._mtx);
                auto it = s._map.find(key);
                if (it != s._map.end()) {
                    s._lru.splice(s._lru.begin(), s._lru, it->second);
                    it->second->second = v;
                } else {
                    s._lru.emplace_front(key, v);
                    s._map.emplace(s._lru.front().first, s._lru.begin());

                    if (s._map.size() > _capacity) {
                        const K& victim_key = s._lru.back().first;
                        s._map.erase(victim_key);
                        s._lru.pop_back();
                    }
                }
                s.inflight.erase(key);
            }

            p.set_value(v);
            return fut;
        } catch (...) {
            {
                std::unique_lock<std::mutex> lock(s._mtx);
                s.inflight.erase(key);
            }
            p.set_exception(std::current_exception());
            throw;
        }
    }

private:
    std::vector<std::unique_ptr<Shard<K, V, Hash, Eq>>> _shards;
    std::size_t _capacity;

    Hash _hasher;

    std::size_t get_shard(const K& k) const {
        return _hasher(k) % _shards.size();
    }
};
