# Sharded LRU Cache
An implementation of a thread-safe, sharded, approximate LRU in-memory cache written in C++20. I implemented this project to gain more experience with concurrency and using modern C++20 features like constraints. 

## Overview

This project implements a concurrent LRU cache with two goals:

1. Reduce lock contention by partitioning the cache into independent shards.
2. Avoid duplicate work on cache misses by providing a `get_or_compute` API that coalesces concurrent lookups for the same key.

The cache is parameterized as `LRUCache<K, V, capacity, num_shards, Hash, Eq>`. The key type must satisfy the `HashableKey` concept and the value type must satisfy the `StorableValue` concept, which currently requires the value to be copyable. The StorableValue is important because `get` copies values into an output parameter and `get_or_compute` stores values in both the cache and a future result. A move-only value type would require a different interface or an internal ownership strategy based on indirection.

This is an approximate LRU rather than a globally exact LRU. Eviction happens independently inside each shard, so the cache preserves recency order per shard instead of across the entire cache. That tradeoff keeps the implementation simple and improves concurrency because unrelated keys do not contend on one global lock.

Below the underlying data structures and design choices are described.

## Cache Interface

The public interface lives in src/cache.h.

The cache supports the standard operations you would expect from an in-memory cache:

1. `get(const K&, V&)` looks up a key, writes the value into the output parameter, and promotes the entry to most recently used within its shard.
2. `put(const K&, V)` inserts or updates an entry. It returns `true` when the key was newly inserted and `false` when the key already existed and was overwritten.
3. `contains(const K&)` checks whether a key is currently stored.
4. `erase(const K&)` removes a key if it exists.
5. `clear_shard(const K&)` clears only the shard that would contain the given key.
6. `clear_all()` clears the entire cache.
7. `size_shard(const K&)`, `capacity_per_shard()`, and `shard_count()` expose shard-level capacity information.

`get_or_compute` returns a `std::shared_future<V>`. If the key is already cached, the function immediately returns a ready future. If the key is missing and no other thread is computing it, the caller becomes the producer and runs the supplied function. If another thread is already computing that same key, later callers do not recompute the value; they attach to the existing shared future and wait for the original computation to finish.

This gives the cache a single-flight style interface: many threads can race to fill the same missing key, but only one of them actually performs the expensive work.

## Shards

The cache stores its shards in a `std::vector<std::unique_ptr<Shard<...>>>`. Each key is assigned to a shard by hashing the key and taking `hash(key) % num_shards`.

That design has a few consequences:

1. Each shard has its own mutex, so operations on different shards can proceed independently.
2. The implementation does not need a global cache lock for ordinary reads and writes.
3. Capacity is tracked per shard, not globally. 

The third point is why the cache is described as approximate LRU. Once the key has been mapped to a shard, all recency tracking and eviction decisions happen locally inside that shard. 

The shard implementation lives in src/shard.h. Each shard contains four data members:

1. A `std::mutex` named `_mtx`.
2. An `std::unordered_map<K, It, Hash, Eq>` named `_map`.
3. A `std::list<std::pair<K, Val>>` named `_lru`.
4. An `std::unordered_map<K, std::shared_future<Val>, Hash, Eq>` named `inflight`.

This `unordered_map + list` combination is the standard way to implement O(1) LRU operations.

## `get_or_compute` and In-Flight Work Deduplication

When `get_or_compute(key, fn)` is called, the cache does the following:

1. Lock the relevant shard.
2. If the key is already cached, return a ready `std::shared_future` containing the stored value.
3. If the key is not cached but appears in `inflight`, return the existing shared future so the caller waits on the current producer.
4. Otherwise create a promise/future pair, publish the shared future in `inflight`, unlock the shard, and run `fn()`.
5. On success, reacquire the shard lock, insert the computed value into the LRU state, erase the `inflight` record, and fulfill the promise.
6. On failure, erase the `inflight` record, store the exception in the promise, and rethrow.

The expensive computation is performed outside the shard lock. That is necessary because the whole point of the API is to keep other threads from blocking on one slow function call any longer than needed.

Additionally, exceptions are shared just like successful values. If the producer throws, all waiters observing the same shared future see the same exception. The key is also removed from `inflight`, which allows a later caller to try computing the value again.

One important thing to note is that the value is not considered cached until the computation finishes and the result is inserted into the LRU structures.

## Testing

The tests cover both single-threaded and concurrent behavior:

1. Basic insertion, lookup, overwrite, and eviction.
2. Custom key types with custom hash functions.
3. Shard-local clearing and shard-local capacity accounting.
4. Concurrent reads, writes, erases, and clears.
5. `get_or_compute` single-flight behavior, including exception propagation and retry after failure.

The test files are [tests/single_threaded.cpp](/Users/aryankumar/Desktop/projects/sharded-cache/tests/single_threaded.cpp) and [tests/multi_threaded.cpp](/Users/aryankumar/Desktop/projects/sharded-cache/tests/multi_threaded.cpp).

To build and run the tests:

```bash
make tests
make single_threaded
make multi_threaded
```

The `Makefile` builds with C++20 and enables AddressSanitizer and UndefinedBehaviorSanitizer by default. It also expects GoogleTest to be installed through Homebrew.
