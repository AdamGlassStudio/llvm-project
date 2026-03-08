# REQUIRES: vax
# RUN: llvm-mc -filetype=obj -triple=vax-unknown-netbsdelf -o %t1.o %s
# RUN: printf '.globl _start\n _start: halt\n halt\n halt\n halt' | llvm-mc -filetype=obj -triple=vax-unknown-netbsdelf -o %t2.o -
# RUN: ld.lld -o %t.exe --Ttext=0x1000 --Tdata=0x2000 %t2.o %t1.o
# RUN: llvm-objdump -s -d %t.exe | FileCheck %s

# Check handling of basic VAX relocation types.

  .data
# R_VAX_8
  .byte _byte
# R_VAX_16
  .short _start
# R_VAX_32
  .long _start

# CHECK:      Contents of section .data:
# CHECK-NEXT: 2000 21001000 100000

  .text
  .globl foo
  .globl _byte
  _byte = 0x21
foo:
# R_VAX_PC32: calls to _start with PC-relative longword displacement
  calls $0, _start
# R_VAX_32: absolute reference in movl immediate
  movl $_start, %r0
  halt

# CHECK:      <foo>:
# calls displacement: target 1000, field at 1007, disp = 1000-(1007+4) = -11 = f5ffffff
# CHECK-NEXT: 1004: fb 00 ef f5 ff ff ff
# movl $_start: immediate 8F followed by 0x00001000
# CHECK-NEXT: 100b: d0 ef ef ff ff ff 50
# CHECK-NEXT: 1012: 00
