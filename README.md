# Cache-system

### **Project OverView**

#### A C++ 11 implementation of multiple caching algorithms(LRU, LFU, LRU-K, HashLRU, HashLFU) with a unified interface and workload benchmark tests

---

### **Features**

- Unified `CachePolicy` interface using **Strategy Pattern**
- Template-based, type-safe, generic cache design
- Safe memory management with `std::shared_ptr` and `std::weak_ptr`
- Multi-slice HashLRU / HashLFU for concurrency optimization
- LFU with self-adaptive aging mechanism
- Benchmark suite for different access patterns (Hot Data / Loop / Workload Shift)

---

### **Architecture**

```
CachePolicy  <-- Base Interface
 ├── LruCache
 │    └── LruKCache
 ├── LfuCache
 │    └── HashLfuCache
 └── HashLruCaches
```

---

### **Test**

| Test            | Description                             | Focus             |
| --------------- | --------------------------------------- | ----------------- |
| `HotDataAccess` | Simulate 70% hot / 30% cold data access | Hotspot retention |
| `LoopPattern`   | Sequential + random scan                | Anti-pollution    |
| `WorkloadShift` | Multi-phase changing access             | Adaptability      |

#### Result:

![alt text](image.png)

---

#### Reflection

_It’s common to hear the term cache throughout my learning journey — in operating systems, databases, CPUs, and distributed systems — yet I never truly understood how it actually works. At the same time, I often felt powerless with C++: despite studying it for a while, I couldn’t build anything meaningful, and I knew almost nothing about C++11. So I decided to take a simple, familiar idea — implementing basic LRU and LFU caches — and use it as a way to explore both caching mechanisms and modern C++ itself._

_As I dug deeper, the project naturally evolved: I introduced templates, RAII with smart pointers, and a unified policy interface to better understand object-oriented design. Later, I extended the project with K-LRU, HashLRU, and LFU-Aging, turning it from a small experiment into a structured, replaceable cache framework._
