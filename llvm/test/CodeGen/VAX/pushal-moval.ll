; RUN: llc -mtriple=vax-unknown-netbsdelf -O2 < %s | FileCheck %s
;
; Test PUSHAL/MOVAL peephole: LEA_FI + PUSHL → PUSHAL,
; and standalone LEA_FI → MOVAL (instead of ADDL3).

declare void @use_ptr(ptr)
declare void @use_two_ptrs(ptr, ptr)

; Push address of local variable — LEA_FI + PUSHL → PUSHAL
define void @push_local_addr() {
; CHECK-LABEL: push_local_addr:
; CHECK: pushal {{.*}}(%fp)
; CHECK: calls $1, use_ptr
  %x = alloca i32
  call void @use_ptr(ptr %x)
  ret void
}

; Push addresses of two locals — both should become PUSHAL
define void @push_two_addrs() {
; CHECK-LABEL: push_two_addrs:
; CHECK: pushal {{.*}}(%fp)
; CHECK: pushal {{.*}}(%fp)
; CHECK: calls $2, use_two_ptrs
  %x = alloca i32
  %y = alloca i32
  call void @use_two_ptrs(ptr %x, ptr %y)
  ret void
}

; Return address of local — standalone LEA_FI → MOVAL
define ptr @return_local_addr() {
; CHECK-LABEL: return_local_addr:
; CHECK: moval {{.*}}(%fp), %r0
  %x = alloca i32
  ret ptr %x
}
