// RUN: %clang_cc1 -triple vax-unknown-netbsdelf -emit-llvm -o - %s | FileCheck %s
//
// Verify varargs ABI: CharPtrBuiltinVaList (va_list is char*).
// Args at AP+offset, advanced by (size+3)&~3.

typedef __builtin_va_list va_list;

// CHECK-LABEL: define {{.*}} i32 @sum_va(i32 {{.*}} %count, ...)
int sum_va(int count, ...) {
  va_list ap;
  // va_start: ap points to first vararg (after 'count' on stack).
  // CHECK: call void @llvm.va_start
  __builtin_va_start(ap, count);
  int total = 0;
  for (int i = 0; i < count; i++) {
    total += __builtin_va_arg(ap, int);
  }
  // CHECK: call void @llvm.va_end
  __builtin_va_end(ap);
  return total;
}

// Verify va_list is a simple char pointer (not a struct).
// CHECK: define {{.*}} void @pass_va(ptr {{.*}} %ap)
void pass_va(va_list ap) {
  (void)__builtin_va_arg(ap, int);
}
