// RUN: %clang_cc1 -triple vax-unknown-netbsdelf -emit-llvm -o - %s | FileCheck %s
//
// Verify calling convention: all args on stack via AP, return in R0.

// CHECK-LABEL: define {{.*}} i32 @add_ints(i32 {{.*}} %a, i32 {{.*}} %b)
int add_ints(int a, int b) {
  return a + b;
}

// CHECK-LABEL: define {{.*}} i32 @four_args(i32 {{.*}} %a, i32 {{.*}} %b, i32 {{.*}} %c, i32 {{.*}} %d)
int four_args(int a, int b, int c, int d) {
  return a + b + c + d;
}

// Pointer argument — 32-bit, passed same as int.
// CHECK-LABEL: define {{.*}} i32 @deref_ptr(ptr {{.*}} %p)
int deref_ptr(int *p) {
  return *p;
}

// Float args — passed as 32-bit values on stack.
// CHECK-LABEL: define {{.*}} float @add_floats(float {{.*}} %a, float {{.*}} %b)
float add_floats(float a, float b) {
  return a + b;
}

// Double args — passed as 64-bit values on stack.
// CHECK-LABEL: define {{.*}} double @add_doubles(double {{.*}} %a, double {{.*}} %b)
double add_doubles(double a, double b) {
  return a + b;
}

// i64 return — R0:R1 pair.
// CHECK-LABEL: define {{.*}} i64 @return_i64(i32 {{.*}} %a, i32 {{.*}} %b)
long long return_i64(int a, int b) {
  return (long long)a * b;
}
