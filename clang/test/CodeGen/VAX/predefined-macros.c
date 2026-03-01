// RUN: %clang -target vax-unknown-netbsdelf -E -dM %s | FileCheck %s
//
// Verify all predefined macros for the VAX target.

// CHECK-DAG: #define __vax__ 1
// CHECK-DAG: #define __vax 1
// CHECK-DAG: #define vax 1
// CHECK-DAG: #define __VAX__ 1
