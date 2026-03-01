; RUN: llc -mtriple=vax-unknown-netbsdelf < %s | FileCheck %s

; Test constant materialization patterns

define i32 @const_zero() {
; CHECK-LABEL: const_zero:
; CHECK: clrl %r0
  ret i32 0
}

define i32 @const_one() {
; CHECK-LABEL: const_one:
; CHECK: movl $1, %r0
  ret i32 1
}

define i32 @const_neg1() {
; CHECK-LABEL: const_neg1:
; CHECK: movl $-1, %r0
  ret i32 -1
}

define i32 @const_large() {
; CHECK-LABEL: const_large:
; CHECK: movl $65536, %r0
  ret i32 65536
}

define i32 @const_max() {
; CHECK-LABEL: const_max:
; CHECK: movl $2147483647, %r0
  ret i32 2147483647
}

define i32 @const_min() {
; CHECK-LABEL: const_min:
; CHECK: movl $-2147483648, %r0
  ret i32 -2147483648
}

define i32 @const_255() {
; CHECK-LABEL: const_255:
; CHECK: movl $255, %r0
  ret i32 255
}

; Address of global
@gvar = external global i32

define ptr @addr_of_global() {
; CHECK-LABEL: addr_of_global:
; CHECK: moval gvar, %r0
  ret ptr @gvar
}
