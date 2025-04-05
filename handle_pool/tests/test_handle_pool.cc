#include <cassert>
#include <iostream>

#include <gtest/gtest.h>
#include <optional>

#include "handle_pool/handle_pool.h"

struct TestStruct {
  int elem;
  static int constructor_count;
  static int destructor_count;

  explicit TestStruct() : elem(0) { ++constructor_count; }

  explicit TestStruct(const int &elem) : elem(elem) { ++constructor_count; }

  TestStruct(const TestStruct &other) : elem(other.elem) {
    ++constructor_count;
  }
  TestStruct(TestStruct &&other) noexcept : elem(other.elem) {
    ++constructor_count;
  }
  ~TestStruct() { ++destructor_count; }

  TestStruct &operator=(const TestStruct &) = default;
  TestStruct &operator=(TestStruct &&) = default;
};

int TestStruct::constructor_count = 0;
int TestStruct::destructor_count = 0;

TEST(HandlePoolTest, BasicFunctionalityTest) {
  handle_pool::HandlePool<TestStruct, 1> test_pool;
  EXPECT_EQ(test_pool.Capacity(), 1);
  EXPECT_TRUE(test_pool.Empty());
  EXPECT_EQ(test_pool.Free(), 1);

  const handle_pool::Handle handle1 = test_pool.Create(10);

  EXPECT_FALSE(test_pool.Empty());
  EXPECT_EQ(test_pool.Free(), 0);

  // Validate handle1 usage.
  EXPECT_TRUE(test_pool.IsValid(handle1));
  auto obj = test_pool.Get(handle1);
  EXPECT_TRUE(obj.has_value());
  EXPECT_EQ(obj.value().get().elem, 10);

  // Validate destroying object referenced by handle1.
  EXPECT_TRUE(test_pool.Destroy(handle1));
  obj = test_pool.Get(handle1);
  EXPECT_FALSE(obj.has_value());
  EXPECT_EQ(obj, std::nullopt);

  EXPECT_EQ(test_pool.Free(), 1);
  EXPECT_TRUE(test_pool.Empty());
}

TEST(HandlePoolTest, ModifyItemTest) {
  handle_pool::HandlePool<TestStruct, 1> test_pool;

  handle_pool::Handle handle1 = test_pool.Create(10);

  EXPECT_TRUE(test_pool.IsValid(handle1));
  auto obj = test_pool.Get(handle1);
  EXPECT_TRUE(obj.has_value());
  EXPECT_EQ(obj.value().get().elem, 10);

  obj.value().get().elem = 999;

  {
    // Retrieve again and update
    auto obj2 = test_pool.Get(handle1);
    EXPECT_TRUE(obj2.has_value());
    EXPECT_EQ(obj2.value().get().elem, 999);

    obj2.value().get().elem = 1000;
  }

  // Original handle should have updated value.
  EXPECT_EQ(obj.value().get().elem, 1000);
}

TEST(HandlePoolTest, DanglingUseOfInvalidHandleTest) {
  handle_pool::HandlePool<TestStruct, 1> test_pool;

  handle_pool::Handle handle1 = test_pool.Create(10);
  // Destroy handle
  test_pool.Destroy(handle1);

  // After destruction, handle should be invalid
  EXPECT_FALSE(test_pool.IsValid(handle1));

  // "dangling" usage of handle to Get() should return nullopt
  auto obj = test_pool.Get(handle1);
  EXPECT_FALSE(obj.has_value());

  // Call destroy again on handle, should be no-op.
  test_pool.Destroy(handle1);

  // "dangling" usage of handle to Get() should return nullopt
  auto obj2 = test_pool.Get(handle1);
  EXPECT_FALSE(obj2.has_value());
}

TEST(HandlePoolTest, ReuseSlotTest) {
  handle_pool::HandlePool<TestStruct, 2> test_pool;

  handle_pool::Handle handle1 = test_pool.Create(10);
  handle_pool::Handle handle2 = test_pool.Create(20);
  auto obj2 = test_pool.Get(handle2);

  // Pool is full.
  EXPECT_EQ(test_pool.Free(), 0);

  // Create() should return invalid Handle
  handle_pool::Handle handle_invalid = test_pool.Create(40);
  EXPECT_EQ(handle_pool::Handle::Invalid(), handle_invalid);

  // Get()ing with invalid handle should be nullopt.
  auto obj_invalid = test_pool.Get(handle_invalid);
  EXPECT_FALSE(obj_invalid.has_value());

  // Destroy handle1
  test_pool.Destroy(handle1);
  EXPECT_FALSE(test_pool.IsValid(handle1));

  // Create new object that re-uses handle1's slot.
  handle_pool::Handle handle3 = test_pool.Create(30);

  // Pool is full again.
  EXPECT_EQ(test_pool.Free(), 0);

  auto obj3 = test_pool.Get(handle3);
  EXPECT_TRUE(obj3.has_value());
  EXPECT_EQ(obj3.value().get().elem, 30);

  // Index is same but generation is different.
  EXPECT_EQ(handle1.index, handle3.index);
  EXPECT_NE(handle1.generation, handle3.generation);

  // Ensure handle2 is still valid.
  EXPECT_TRUE(obj2.has_value());
  EXPECT_EQ(obj2.value().get().elem, 20);

  // Ensure Get() on handle1 returns nullopt
  auto obj1 = test_pool.Get(handle1);
  EXPECT_FALSE(obj1.has_value());
}
