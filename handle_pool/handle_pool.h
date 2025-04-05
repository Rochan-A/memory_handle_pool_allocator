#pragma once

#include <array>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <limits>
#include <new>
#include <optional>
#include <sys/types.h>
#include <type_traits>
#include <utility>
#include <vector>

#include "rwlock/rw_lock.h"
#include "rwlock/shared_lock.h"
#include "rwlock/unique_lock.h"

namespace handle_pool {

/*
 * Handle is composed of two 32-bit fields:
 *   - index      : index into the pool array
 *   - generation : age when created
 */
struct Handle {
  const uint32_t index;
  const uint32_t generation;

  Handle(const uint32_t index, const uint32_t generation)
      : index(index), generation(generation) {}

  static const Handle Invalid() {
    return Handle(std::numeric_limits<uint32_t>::max(), 0);
  }

  bool operator==(const Handle &other) const {
    return (index == other.index) && (generation == other.generation);
  }

  bool operator!=(const Handle &other) const { return !(*this == other); }
};

inline std::ostream &operator<<(std::ostream &os, const Handle &h) {
  return os << "Handle { idx: " << h.index << ", gen: " << h.generation << " }";
}

/*
 * A fixed-capacity pool that manages objects of type T and returns
 * lightweight handles to them. The pool uses a free list and a
 * generation counter per slot to detect stale handles.
 *
 * Thread-safety:
 * - `Create`, `Destroy`, and the destructor use an exclusive lock
 * (unique_lock).
 * - `Get` uses a shared lock (shared_lock).
 */
template <typename T, size_t kCapacity> class HandlePool {
public:
  explicit HandlePool() {
    static_assert(kCapacity > 0, "Capacity must be positive");
    free_list_.reserve(kCapacity);
    for (uint32_t i = 0; i < kCapacity; ++i) {
      items_[i].in_use = false;
      items_[i].generation = 0;
      free_list_.push_back(i);
    }
  }

  // Destructor cleans up all used items.
  ~HandlePool() {
    rwlock::UniqueLock ul(rwlock_);
    for (auto &item : items_) {
      if (item.in_use) {
        reinterpret_cast<T *>(&item.storage)->~T();
      }
    }
  }

  // Creates a new T in-place, returning a handle.
  // Exclusive lock because we modify shared data structures.
  template <typename... Args> const Handle Create(Args &&...args) {
    rwlock::UniqueLock l(rwlock_);

    if (free_list_.empty()) {
      return Handle::Invalid();
    }
    const uint32_t slot = free_list_.back();
    free_list_.pop_back();

    Item &item = items_[slot];
    try {
      new (&item.storage) T(std::forward<Args>(args)...);
      item.in_use = true;
    } catch (...) {
      // If constructor throws, put the slot back.
      free_list_.push_back(slot);
      return Handle::Invalid();
    }

    return Handle{slot, item.generation};
  }

  // Destroy the T associated with the handle.
  // Exclusive lock because we modify shared data structures.
  bool Destroy(const Handle &handle) {
    rwlock::UniqueLock l(rwlock_);

    if (!IsValidInternal(handle)) {
      return false;
    }

    Item &item = items_[handle.index];
    reinterpret_cast<T *>(&item.storage)->~T();
    item.in_use = false;
    ++item.generation;
    free_list_.push_back(handle.index);
    return true;
  }

  // Returns an optional reference to T if the handle is valid, else nullopt.
  // Shared lock because we only read shared data.
  std::optional<std::reference_wrapper<T>> Get(const Handle &handle) {
    rwlock::SharedLock l(rwlock_);

    if (!IsValidInternal(handle)) {
      return std::nullopt;
    }
    T &obj = *reinterpret_cast<T *>(&items_[handle.index].storage);
    return std::ref(obj);
  }

  // Const version. Returns an optional reference to const T if valid, else
  // nullopt.
  std::optional<std::reference_wrapper<const T>>
  Get(const Handle &handle) const {
    rwlock::SharedLock l(rwlock_);

    if (!IsValidInternal(handle)) {
      return std::nullopt;
    }
    const T &obj = *reinterpret_cast<const T *>(&items_[handle.index].storage);
    return std::cref(obj);
  }

  // Checks if a given handle is still valid.
  bool IsValid(const Handle &handle) {
    rwlock::SharedLock l(rwlock_);
    return IsValidInternal(handle);
  }

  inline constexpr size_t Capacity() const { return kCapacity; }

  // Returns true if there are no currently used slots.
  bool Empty() {
    rwlock::SharedLock l(rwlock_);
    return (free_list_.size() == kCapacity);
  }

  // Returns how many free slots remain.
  size_t Free() {
    rwlock::SharedLock l(rwlock_);
    return free_list_.size();
  }

  // Disallow copy (owning resource).
  HandlePool(const HandlePool &) = delete;
  HandlePool &operator=(const HandlePool &) = delete;

private:
  struct Item {
    alignas(T) unsigned char storage[sizeof(T)];
    uint32_t generation;
    bool in_use;
  };

  // Checks validity without locking (callers must hold a lock).
  const bool IsValidInternal(const Handle &handle) const {
    if (handle.index >= kCapacity || handle == Handle::Invalid()) {
      return false;
    }
    const Item &item = items_[handle.index];
    return item.in_use && (item.generation == handle.generation);
  }

  std::array<Item, kCapacity> items_;
  std::vector<uint32_t> free_list_;

  // Protect all shared data (items_ and free_list_).
  rwlock::RWLock rwlock_;
};

} // namespace handle_pool
