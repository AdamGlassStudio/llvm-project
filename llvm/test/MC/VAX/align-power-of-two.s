// RUN: llvm-mc -triple=vax -filetype=obj %s -o %t.o
// RUN: llvm-objdump -d %t.o | FileCheck %s

// Verify that .align uses power-of-two interpretation (matching GAS).
// .align 2 = 2^2 = 4-byte alignment, NOT 2-byte alignment.
// This is critical for SCB vector tables in NetBSD/vax.

  .text
  .globl start
start:
  nop                         // 1 byte at offset 0
  .align 2                    // should pad to offset 4 (4-byte boundary)
handler1:
  nop                         // should be at offset 4

// CHECK-LABEL: <start>:
// CHECK:       0: 01          nop
// CHECK:       4: 01          nop

  nop                         // offset 5
  nop                         // offset 6
  .align 2                    // should pad to offset 8
handler2:
  nop                         // should be at offset 8

// CHECK:       5: 01          nop
// CHECK:       6: 01          nop
// CHECK:       8: 01          nop

  .align 3                    // 2^3 = 8-byte alignment, pad to offset 16
handler3:
  nop

// CHECK:      10: 01          nop
