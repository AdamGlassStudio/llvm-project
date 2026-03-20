// RUN: %clang -target vax-unknown-netbsdelf -### %s 2>&1 | FileCheck %s
// CHECK: "-triple" "vax-unknown-netbsdelf"

// RUN: %clang -target vax-unknown-netbsdelf -dM -E %s | FileCheck --check-prefix=MACROS %s
// MACROS-DAG: #define __vax__ 1
// MACROS-DAG: #define __VAX__ 1

// VAX ELF defaults to PIC (matching GCC).
// RUN: %clang -target vax-unknown-netbsdelf -### %s 2>&1 | FileCheck --check-prefix=PIC %s
// PIC: "-mrelocation-model" "pic"
