// RUN: %clang_cc1 -triple vax-unknown-netbsdelf -O2 -emit-llvm -o - %s | FileCheck %s

#include <stdatomic.h>

// CHECK-LABEL: define{{.*}} void @test_store_relaxed
// CHECK-NOT: call{{.*}}__atomic_store
// CHECK: store atomic i32 %val, ptr %p monotonic
void test_store_relaxed(_Atomic unsigned int *p, unsigned int val) {
  atomic_store_explicit(p, val, memory_order_relaxed);
}

// CHECK-LABEL: define{{.*}} void @test_store_seqcst
// CHECK-NOT: call{{.*}}__atomic_store
// CHECK: store atomic i32 %val, ptr %p seq_cst
void test_store_seqcst(_Atomic unsigned int *p, unsigned int val) {
  atomic_store_explicit(p, val, memory_order_seq_cst);
}

// CHECK-LABEL: define{{.*}} i32 @test_load_relaxed
// CHECK-NOT: call{{.*}}__atomic_load
// CHECK: load atomic i32, ptr %p monotonic
unsigned int test_load_relaxed(_Atomic unsigned int *p) {
  return atomic_load_explicit(p, memory_order_relaxed);
}

// CHECK-LABEL: define{{.*}} i1 @test_cas
// CHECK-NOT: call{{.*}}__atomic_compare_exchange
// CHECK: cmpxchg weak ptr
_Bool test_cas(_Atomic unsigned int *p, unsigned int expected, unsigned int desired) {
  return atomic_compare_exchange_weak_explicit(p, &expected, desired,
    memory_order_relaxed, memory_order_relaxed);
}
