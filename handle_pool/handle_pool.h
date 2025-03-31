#pragma once

#include <array>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <limits>
#include <new>
#include <optional>
#include <shared_mutex>
#include <type_traits>
#include <utility>
#include <vector>

namespace mem_handle {

/*
 * Handle is composed of two 32-bit fields:
 *   - index      : index into the pool array
 *   - generation : age when created
 */
struct Handle {
  uint32_t index;
  uint32_t generation;

  bool IsValid() const { return index != std::numeric_limits<uint32_t>::max(); }

  static Handle Invalid() {
    return Handle{std::numeric_limits<uint32_t>::max(), 0};
  }

  bool operator==(const Handle &other) const {
    return (index == other.index) && (generation == other.generation);
  }

  bool operator!=(const Handle &other) const { return !(*this == other); }
};

inline std::ostream &operator<<(std::ostream &os, const Handle &h) {
  return os << "Handle { idx: " << h.index << ", gen: " << h.generation << " }";
}

// Forward-declare HandlePool so our view types can friend it.
template <typename T, size_t kCapacity> class HandlePool;

/*
 * ObjectView is a move-only "ticket" that safely refers to a T inside
 * the pool. It holds a shared_lock so the underlying slot cannot be
 * destroyed while this view is alive.
 *
 * - Non-copyable but movable.
 * - operator->() and operator*() provide direct access to T.
 */
template <typename T> class ObjectView {
public:
  // Non-copyable, but movable
  ObjectView(const ObjectView &) = delete;
  ObjectView &operator=(const ObjectView &) = delete;

  ObjectView(ObjectView &&) = default;
  ObjectView &operator=(ObjectView &&) = default;

  ~ObjectView() = default;

  // Access to the underlying object
  T *operator->() { return ptr_; }
  T &operator*() { return *ptr_; }

private:
  // Only HandlePool can create one
  friend class HandlePool<T, 1>; // We'll friend the template with any capacity
                                 // below

  template <T, size_t N>
  friend class HandlePool; // friend the entire template with capacity param

  // Private constructor
  ObjectView(T *ptr, std::shared_lock<std::shared_mutex> lock)
      : ptr_(ptr), lock_(std::move(lock)) {}

  T *ptr_;
  // The shared lock that keeps the pool from destroying this slot.
  std::shared_lock<std::shared_mutex> lock_;
};

/*
 * ConstObjectView is the const version of ObjectView.
 * It provides read-only access to a const T*.
 */
template <typename T> class ConstObjectView {
public:
  // Non-copyable, but movable
  ConstObjectView(const ConstObjectView &) = delete;
  ConstObjectView &operator=(const ConstObjectView &) = delete;

  ConstObjectView(ConstObjectView &&) = default;
  ConstObjectView &operator=(ConstObjectView &&) = default;

  ~ConstObjectView() = default;

  // Access to the underlying object
  const T *operator->() const { return ptr_; }
  const T &operator*() const { return *ptr_; }

private:
  friend class HandlePool<T, 1>;

  template <size_t N> friend class HandlePool; // friend the entire template

  ConstObjectView(const T *ptr, std::shared_lock<std::shared_mutex> lock)
      : ptr_(ptr), lock_(std::move(lock)) {}

  const T *ptr_;
  std::shared_lock<std::shared_mutex> lock_;
};

/*
 * A fixed-capacity pool that manages objects of type T and returns
 * lightweight handles to them. The pool uses a free list and a
 * generation counter per slot to detect stale handles.
 *
 * - Storage for each T is allocated in-place (no heap allocs per object).
 * - `Create(...)` placement-news the object at a free slot and returns
 *   a unique {index, generation} handle.
 * - `Destroy(handle)` calls the object's destructor, increments the slot's
 *   generation, and puts the slot back on the free list.
 * - `Get(handle)` returns an optional noncopyable "ObjectView" or
 * "ConstObjectView" that safely references T while holding a shared lock. Once
 * the view destructs, the lock is released, allowing destruction or reuse.
 *
 * Thread-safety:
 * - `Create`, `Destroy`, and destructor use an exclusive lock (unique_lock).
 * - `Get` (and `IsValid`) use a shared lock (shared_lock).
 */
template <typename T, size_t kCapacity> class HandlePool {
public:
  // Disallow copying to avoid accidental duplication.
  HandlePool(const HandlePool &) = delete;
  HandlePool &operator=(const HandlePool &) = delete;

  HandlePool() {
    free_list_.reserve(kCapacity);
    for (uint32_t i = 0; i < kCapacity; ++i) {
      items_[i].in_use = false;
      items_[i].generation = 0;
      free_list_.push_back(i);
    }
  }

  ~HandlePool() {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    for (uint32_t i = 0; i < kCapacity; ++i) {
      if (items_[i].in_use) {
        reinterpret_cast<T *>(&items_[i].storage)->~T();
      }
    }
  }

  // Creates a new T in-place, returning a handle or Handle::Invalid() if no
  // space remains.
  template <typename... Args> Handle Create(Args &&...args) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    if (free_list_.empty()) {
      return Handle::Invalid();
    }
    const uint32_t slot = free_list_.back();
    free_list_.pop_back();

    Item &item = items_[slot];
    new (&item.storage) T(std::forward<Args>(args)...);
    item.in_use = true;
    return Handle{slot, item.generation};
  }

  // Destroys the object associated with 'handle', if valid.
  void Destroy(const Handle &handle) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    if (!IsValidInternal(handle)) {
      return;
    }
    Item &item = items_[handle.index];
    reinterpret_cast<T *>(&item.storage)->~T();
    item.in_use = false;
    ++item.generation; // Invalidate old handles
    free_list_.push_back(handle.index);
  }

  // Returns a non-copyable, move-only view of the object if 'handle' is valid;
  // otherwise nullopt. Holding this view keeps a shared lock on the pool, so
  // the slot cannot be destroyed during the view's lifetime.
  std::optional<ObjectView<T>> Get(const Handle &handle) {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    if (!IsValidInternal(handle)) {
      return std::nullopt;
    }
    T *ptr = reinterpret_cast<T *>(&items_[handle.index].storage);
    // Construct a non-copyable view that holds this shared_lock
    return ObjectView<T>(ptr, std::move(lock));
    // Note: We move the lock *into* the ObjectView. Once returned, this
    // HandlePool no longer holds it.
    //       That means the scope of this method does not keep the lock. The
    //       user does, in the ObjectView.
  }

  // Const variant: returns a read-only view if 'handle' is valid.
  std::optional<ConstObjectView<T>> Get(const Handle &handle) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    if (!IsValidInternal(handle)) {
      return std::nullopt;
    }
    const T *ptr = reinterpret_cast<const T *>(&items_[handle.index].storage);
    return ConstObjectView<T>(ptr, std::move(lock));
  }

  // Simple validity test. Note that the slot could become invalid after this
  // call if another thread destroys it. For actual usage, prefer using Get(...)
  // so you hold a shared lock during access.
  bool IsValid(const Handle &handle) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return IsValidInternal(handle);
  }

  size_t Capacity() const { return kCapacity; }

private:
  // Checks validity without locking (callers must hold a lock).
  bool IsValidInternal(const Handle &handle) const {
    if (!handle.IsValid() || handle.index >= kCapacity) {
      return false;
    }
    const Item &item = items_[handle.index];
    return item.in_use && (item.generation == handle.generation);
  }

  struct Item {
    alignas(T) unsigned char storage[sizeof(T)];
    uint32_t generation;
    bool in_use;
  };

  std::shared_mutex mutex_;
  std::array<Item, kCapacity> items_;
  std::vector<uint32_t> free_list_;
};

} // namespace mem_handle
