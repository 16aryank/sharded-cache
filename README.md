# sharded-cache
A thread-safe, sharded, approximate LRU in-memory cache

## About

This is a thread-safe LRU cache that shards data to reduce lock-contention. It is approximately LRU because it uses per-shared eviction rather than global eviction.

The project is mostly done, but I have to add more documentation, stress tests, and more thouroughly check thread-safety via the ThreadSanitizer (TSAN).