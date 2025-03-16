#pragma once

#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <limits>
#include <vector>

/*
 * A Handle is composed of two 16-bit fields:
 *   - index_      : The slot index into the pool array
 *   - generation_ : Used to detect "dangling" or "stale" handles
 */
struct Handle {
  // Lower 16 bits
  uint32_t index_ : 16;
  // Upper 16 bits
  uint32_t generation_ : 16;

  // Returns whether this handle is considered valid (not the "sentinel" index).
  inline bool IsValid() const {
    return index_ != std::numeric_limits<uint16_t>::max();
  }

  // Create an "invalid" handle that signals error or uninitialized.
  static inline Handle Invalid() {
    return Handle{std::numeric_limits<uint16_t>::max(), 0};
  }

  inline bool operator==(const Handle &other) const {
    return (index_ == other.index_) && (generation_ == other.generation_);
  }

  inline bool operator!=(const Handle &other) const {
    return !(*this == other);
  }
};

inline std::ostream &operator<<(std::ostream &os, const Handle &h) {
  os << "Handle { idx:" << h.index_ << ", gen:" << h.generation_ << " }";
  return os;
}

/*
 * A pool that manages objects of type T in a fixed-capacity array.
 *
 *  - Objects of type T are stored contiguously (std::array<T, kCapacity>).
 *  - A matching array (std::array<uint16_t, kCapacity>) holds "generation"
 *    counters for each slot, used to detect stale handles.
 *  - A free list (std::vector<uint32_t>) stores indices of unused slots.
 *
 * Usage:
 *    - Use Create(...) to construct a new T in-place. Returns a Handle.
 *    - Use Destroy(handle) to destroy that object, making its slot reusable.
 *    - Use Get(handle) to retrieve a pointer (valid until next Destroy()).
 *    - Use IsValid(handle) to check whether a handle still points to a
 *      valid, un-destroyed object in the pool.
 *
 * This arrangement avoids many small dynamic allocations and catches
 * stale or "dangling" handle usages via generation checks.
 */
template <typename T, size_t kCapacity> class Pool {
public:
  Pool() : capacity_(kCapacity) {
    // Initialize all generation counters to 0.
    generations_.fill(0);

    free_list_.reserve(capacity_);
    // Initially, every slot is free.
    for (uint32_t i = 0; i < capacity_; ++i) {
      free_list_.push_back(i);
    }
  }

  // Creates and constructs a new object of type T, returning a Handle to it.
  template <typename... Args> Handle Create(Args &&...args) {
    if (free_list_.empty()) {
      // Pool is full; in production code, you might grow arrays or handle
      // error.
      return Handle::Invalid();
    }

    // Acquire a free slot index from the back of the free list.
    uint32_t slot_index = free_list_.back();
    free_list_.pop_back();

    // Current generation for that slot.
    uint16_t gen = generations_[slot_index];

    // Placement-new the object into that slot.
    new (&objects_[slot_index]) T(std::forward<Args>(args)...);

    // Build and return the handle.
    Handle handle;
    handle.index_ = static_cast<uint16_t>(slot_index);
    handle.generation_ = gen;
    return handle;
  }

  // Destroys the object referenced by 'handle', if valid, returning
  // its slot to the free list and incrementing the slot's generation.
  void Destroy(const Handle &handle) {
    if (!IsValid(handle)) {
      return;
    }

    // Destruct the in-place object.
    objects_[handle.index_].~T();
    // Increment generation so stale handles no longer match.
    ++generations_[handle.index_];

    // Mark the slot as free.
    free_list_.push_back(handle.index_);
  }

  // Returns a pointer to the object if 'handle' is still valid. Otherwise null.
  inline T *Get(const Handle &handle) {
    if (!IsValid(handle)) {
      return nullptr;
    }
    return &objects_[handle.index_];
  }

  // Const version of Get().
  inline const T *Get(const Handle &handle) const {
    if (!IsValid(handle)) {
      return nullptr;
    }
    return &objects_[handle.index_];
  }

  // Check whether a given handle refers to a valid, un-destroyed object.
  inline bool IsValid(const Handle &handle) const {
    if (!handle.IsValid()) {
      return false;
    }
    if (handle.index_ >= capacity_) {
      return false;
    }
    uint16_t current_gen = generations_[handle.index_];
    // If the handle's generation is behind the current generation,
    // it's stale (i.e., the slot was reused).
    return (handle.generation_ == current_gen);
  }

  // Returns the fixed capacity of the pool.
  inline size_t Capacity() const { return capacity_; }

private:
  const size_t capacity_;
  std::array<T, kCapacity> objects_;
  std::array<uint16_t, kCapacity> generations_;
  std::vector<uint32_t> free_list_;
};
