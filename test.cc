// mem_handle_test.cc
#include <cassert>
#include <iostream>

#include "mem_handle.hpp"

struct TestObject {
  int x_;

  TestObject() : x_(0) {}

  // Overloaded constructor
  explicit TestObject(int val) : x_(val) {
    std::cout << "[TestObject ctor] x=" << x_ << "\n";
  }

  ~TestObject() { std::cout << "[TestObject dtor] x=" << x_ << "\n"; }
};

int main() {
  Pool<TestObject, 4> test_pool;

  Handle handle1 = test_pool.Create(10);
  Handle handle2 = test_pool.Create(20);
  Handle handle3 = test_pool.Create(30);

  std::cout << "Created handles:\n"
            << "  handle1 = " << handle1 << "\n"
            << "  handle2 = " << handle2 << "\n"
            << "  handle3 = " << handle3 << "\n\n";

  // Use short-lived pointers for reading/writing
  if (TestObject *obj = test_pool.Get(handle1)) {
    std::cout << "handle1 points to object with x_=" << obj->x_ << "\n";
    obj->x_ = 999; // modify
  } else {
    std::cout << "ERROR: handle1 invalid immediately after creation.\n";
  }

  // Destroy handle2
  std::cout << "\nDestroying handle2...\n";
  test_pool.Destroy(handle2);

  // After destruction, handle2 should be invalid
  assert(!test_pool.IsValid(handle2) &&
         "handle2 should be invalid immediately after destruction!");

  // "dangling" usage of handle2: Get() should return nullptr
  if (TestObject *obj = test_pool.Get(handle2)) {
    // Should never happen if the generation check is correct
    std::cout << "ERROR: We got a valid pointer for a destroyed handle!\n";
  } else {
    std::cout << "OK: handle2 is now invalid and returns nullptr\n";
  }

  // Reuse handle2's slot
  std::cout << "\nCreating new object (reuse handle2's slot)...\n";
  Handle handle4 = test_pool.Create(1234);

  std::cout << "  handle4 = " << handle4 << "\n";
  if (TestObject *obj4 = test_pool.Get(handle4)) {
    std::cout << "  handle4 points to object with x_=" << obj4->x_ << "\n";
  } else {
    std::cout << "ERROR: handle4 not valid right after creation.\n";
  }

  // Ensure handle1 is still valid, and read the updated value
  if (TestObject *obj1 = test_pool.Get(handle1)) {
    std::cout << "  handle1 (still valid) points to x_=" << obj1->x_ << "\n";
    assert(obj1->x_ == 999 &&
           "handle1's object should retain the updated value 999");
  } else {
    std::cout << "ERROR: handle1 unexpectedly invalid.\n";
  }

  // Destroy everything
  std::cout << "\nDestroying handle1, handle3, handle4...\n";
  test_pool.Destroy(handle1);
  test_pool.Destroy(handle3);
  test_pool.Destroy(handle4);

  // handle3 or handle4 are now destroyed, so Get() should be null.
  TestObject *obj3 = test_pool.Get(handle3);
  TestObject *obj4_again = test_pool.Get(handle4);

  std::cout << "handle3->Get() => "
            << (obj3 ? "VALID (ERROR!)" : "nullptr (ok)") << "\n";
  std::cout << "handle4->Get() => "
            << (obj4_again ? "VALID (ERROR!)" : "nullptr (ok)") << "\n";

  // Create more objects and fill up the pool
  std::cout << "\nTesting pool capacity usage...\n";
  std::vector<Handle> handles;
  handles.reserve(test_pool.Capacity());
  for (size_t i = 0; i < test_pool.Capacity(); ++i) {
    Handle h = test_pool.Create(i * 100);
    // If the pool is not full, we expect a valid handle
    if (h.IsValid()) {
      TestObject *obj = test_pool.Get(h);
      assert(obj && "Object pointer should not be null after creation");
      assert(obj->x_ == static_cast<int>(i * 100) &&
             "Object value mismatch in capacity test");
      handles.push_back(h);
    } else {
      std::cout << "ERROR: Could not create a new object at iteration " << i
                << ". The pool is unexpectedly full.\n";
    }
  }
  // The pool should now be full; a subsequent Create should fail.
  Handle extra_handle = test_pool.Create(9999);
  if (extra_handle.IsValid()) {
    std::cout << "ERROR: The pool unexpectedly allowed creation beyond "
                 "its capacity.\n";
  } else {
    std::cout << "Good: The pool is full, further creation returns invalid.\n";
  }

  for (Handle h : handles) {
    test_pool.Destroy(h);
  }

  return 0;
}
