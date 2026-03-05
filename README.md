
# Mcache

**Mcache** is a high‑performance in‑memory cache database written in C++ that implements a custom binary protocol (similar in spirit to HTTP framing) using low‑level socket programming.  
The project focuses on **performance, custom data structures, and system‑level design**, implementing a Redis‑like feature set while exploring **intrusive data structures, serialization formats, and efficient memory management**.

---

# Table of Contents

1. Overview
2. Features
3. Architecture
4. Core Data Structures
5. Protocol Design
6. Supported Commands
7. Client Example
8. Performance Benchmarks
9. HashTable Collision Attack Test
10. Cache vs Database Benchmark
11. Project Structure
12. How to Run

---

# Overview

Mcache is designed as a **learning-oriented high-performance cache server** that demonstrates how systems like Redis work internally.

Key goals of the project:

- Implement a **custom TCP protocol**
- Design **high-performance data structures**
- Explore **intrusive containers**
- Handle **TTL and timeouts efficiently**
- Implement **sorted sets with order statistic trees**
- Benchmark against **Redis and MongoDB**

The system supports **string values and sorted sets**, along with expiration management and asynchronous deletion for large containers.

---

# Features

## Custom TCP Protocol

Mcache uses a **custom binary protocol** implemented over raw sockets.

Features:

- TCP based
- Message framing similar to HTTP length prefix
- Supports multiple commands per request
- Efficient binary serialization

Message framing format:

```
[4B: body length][body]
```

---

## Intrusive Resizable HashTable

The cache uses a **custom resizable hash table** instead of `std::unordered_map`.

Goals:

- Prevent collision attack vulnerabilities
- Reduce lookup latency
- Support intrusive node storage

Advantages:

- Avoids separate allocations
- Better cache locality
- Direct node embedding

---

## Data Serialization (TLV Format)

Mcache serializes responses using a **Type‑Length‑Value (TLV)** format.

Supported types:

| Tag | Type |
|----|----|
| 0x00 | NIL |
| 0x01 | Error |
| 0x02 | String |
| 0x03 | Integer |
| 0x04 | Double |
| 0x05 | Array |

Example:

```
[TYPE][LENGTH][VALUE]
```

This allows the protocol to support **multiple value types efficiently**.

---

## Sorted Set (ZSET)

The sorted set implementation is built using **multi-index intrusive data structures**:

Components:

- AVL Tree (ordered by score)
- Hash table (lookup by member)

This allows:

| Operation | Complexity |
|---|---|
| Lookup | O(1) |
| Insert | O(log n) |
| Range query | O(log n + k) |

---

## TTL and Expiration

Mcache supports **key expiration using TTL**.

Two mechanisms are used:

### Heap-Based TTL

A min‑heap tracks the nearest expiration time.

Benefits:

- Efficient expiration scheduling
- O(log n) updates

### OS Timers

Connection idle timeouts are handled using **OS timers**.

---

## Thread Pool for Large Deletions

Deleting large sorted sets can be expensive.

To prevent blocking the main event loop:

- Small containers → deleted synchronously
- Large containers → deleted asynchronously

Large containers are offloaded to a **thread pool**.

Threshold example:

```
> 1000 elements → async deletion
```

---

# Architecture

The system architecture includes:

- TCP server
- Event loop
- Request parser
- Command dispatcher
- Storage engine
- TTL manager

Main components:

```
Client
  │
  ▼
TCP Socket Server
  │
  ▼
Request Parser
  │
  ▼
Command Handler
  │
  ▼
Storage Engine
  │
  ├── HashTable
  ├── Sorted Set (AVL + Hash)
  └── TTL Heap
```

---

# Wire Protocol Reference

## General Frame

Every message follows this structure:

```
[4B: body length][body]
```

---

# Request Format

```
[4B body length]
[4B nstr]
[4B len₁][str₁]
[4B len₂][str₂]
...
```

Example command:

```
SET key value
```

Encoded as:

```
|3|3|set|3|key|5|value|
```

---

# Response Tags

| Tag | Meaning |
|----|----|
| NIL | 0x00 |
| ERR | 0x01 |
| STR | 0x02 |
| INT | 0x03 |
| DBL | 0x04 |
| ARR | 0x05 |

---


# Commands

## GET

Retrieve the value of a key.

### Request

```
["get", key]
```

Wire format

```
[4B body][4B 0x02]
[4B 0x03]["get"]
[4B key_length][key]
```

### Responses

Value Found

```
[1B TAG_STR][4B length][value]
```

Example

```
STR "mahmoud"
```

---

Key Not Found

```
[1B TAG_NIL]
```

Example

```
NIL
```

---

Wrong Type

```
[1B TAG_ERR][ERR_BAD_TYP]["not a string value"]
```

---

## SET

Store a string value.

### Request

```
["set", key, value]
```

Wire format

```
[4B body][4B 0x03]
[4B 0x03]["set"]
[key]
[value]
```

### Response

```
NIL
```

If type mismatch:

```
ERR_BAD_TYP
```

---

## DEL

Delete a key.

### Request

```
["del", key]
```

### Responses

Deleted

```
INT 1
```

Not Found

```
INT 0
```

---

## KEYS

Return all keys.

### Request

```
["keys"]
```

### Response

```
ARR [key1, key2, ...]
```

Wire format

```
[1B TAG_ARR]
[4B count]
[key1]
[key2]
...
```

---

## PEXPIRE

Set TTL in milliseconds.

### Request

```
["pexpire", key, ttl_ms]
```

Example

```
pexpire session 5000
```

### Responses

TTL Set

```
INT 1
```

Key Not Found

```
INT 0
```

Invalid TTL

```
ERR_BAD_ARG
```

---

## PTTL

Return remaining TTL in milliseconds.

### Request

```
["pttl", key]
```

### Responses

Key Not Found

```
INT -2
```

No TTL

```
INT -1
```

Has TTL

```
INT remaining_ms
```

Example

```
PTTL session
→ 2450
```

---

## ZADD

Add or update member in sorted set.

Sorted set implementation:

- AVL Tree (ordering)
- HashTable (lookup)

### Request

```
["zadd", key, score, member]
```

Example

```
zadd leaderboard 100 player1
```

### Responses

Added

```
INT 1
```

Updated

```
INT 0
```

Invalid Score

```
ERR_BAD_ARG
```

Wrong Type

```
ERR_BAD_TYP
```

---

## ZREM

Remove member from sorted set.

### Request

```
["zrem", key, member]
```

### Responses

Removed

```
INT 1
```

Not Found

```
INT 0
```

Wrong Type

```
ERR_BAD_TYP
```

---

## ZSCORE

Return score of member.

### Request

```
["zscore", key, member]
```

### Responses

Found

```
DBL score
```

Not Found

```
NIL
```

Wrong Type

```
ERR_BAD_TYP
```

---

## ZQUERY

Range query on sorted set.

### Request

```
["zquery", key, score, member, offset, limit]
```

Example

```
zquery leaderboard 100 player1 0 10
```

### Response

```
ARR [
  name1, score1,
  name2, score2,
  ...
]
```

Empty

```
ARR []
```

Invalid Args

```
ERR_BAD_ARG
```



# Client Example (Node.js)

Example client usage:

```javascript
const CacheClient = require("./cacheClient");

async function main() {

    const cache = new CacheClient("127.0.0.1", 1234);

    await cache.connect();

    await cache.set("name", "mahmoud");

    const value = await cache.get("name");
    console.log(value);

    await cache.zadd("scores", 100, "player1");

    const score = await cache.zscore("scores", "player1");
    console.log(score);
}

main();
```

---

# Performance Benchmarks

Redis vs Mcache

| Test | MCache/s | Redis/s | Winner |
|----|----|----|----|
| SET sequential | 6366 | 1242 | 5.13x Mcache |
| GET sequential | 7563 | 1131 | 6.68x Mcache |
| SET pipeline | 40134 | 16940 | 2.37x Mcache |
| GET pipeline | 53511 | 23714 | 2.26x Mcache |
| ZADD sequential | 7854 | 1126 | 6.97x Mcache |
| ZADD pipeline | 29974 | 17674 | 1.70x Mcache |
| DEL pipeline | 52983 | 22373 | 2.37x Mcache |

---

## Large Sorted Set Deletion

Test: ZSET with **2000 members**

| System | Time |
|----|----|
| Mcache | 232 µs |
| Redis | 1929 µs |

Mcache is **8.3x faster** for large key deletion.

---

# HashTable Collision Attack Test

Comparison between:

- Custom Resizable HashTable
- std::unordered_map

Results

```
MCache HashTable
SET: 2377 ms
GET: 2260 ms

std::unordered_map
SET: 2008 ms
GET: 1831 ms
```

Even under attack conditions, the custom structure remains **competitive while providing collision resistance**.

---

# Cache vs Database Benchmark

Comparison with MongoDB queries.

| System | Avg Latency |
|----|----|
| MongoDB | 11.56 ms |
| Mcache | 0.47 ms |

Mcache is **24.65x faster than MongoDB** for cached queries.

---

# Project Structure

Example structure:

```
Mcache
│
├── main.cpp
├── hashtable.h
├── zset.h
├── heap.h
├── list.h
├── thread_pool.h
├── common.h
│
├── client
│   └── cacheClient.js
│
└── test
    └── benchmark
```

---

# How to Run

Compile:

```
g++ main.cpp -lws2_32 -o mcache
```

Run server:

```
./mcache
```

# Educational Value

This project demonstrates:

- Network protocol design
- Event driven server architecture
- Intrusive data structures
- Memory management
- Cache design principles
- Performance benchmarking

It is designed as a **deep dive into how high‑performance systems like Redis work internally**.
