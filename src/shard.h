#pragma once

#include <concepts>
#include <functional>
#include <list>
#include <mutex>
#include <future>
#include <unordered_map>

template <class K, class Val,
          class Hash = std::hash<K>,
          class Eq   = std::equal_to<K>>
struct Shard {
    using List = std::list<std::pair<K, Val>>;
    using It   = typename List::iterator;

    mutable std::mutex _mtx;
    std::unordered_map<K, It, Hash, Eq> _map;  // key -> iterator into lru
    List _lru;                                 // front = MRU, back = LRU
    std::unordered_map<K, std::shared_future<Val>, Hash, Eq> inflight;
};
