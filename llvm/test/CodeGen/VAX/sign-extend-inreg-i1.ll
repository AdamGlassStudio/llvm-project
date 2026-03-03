; RUN: llc -mtriple=vax-unknown-netbsdelf < %s | FileCheck %s

; Test SIGN_EXTEND_INREG i1 — sign-extend a single bit.

define i32 @sext_inreg_i1(i32 %x) {
; CHECK-LABEL: sext_inreg_i1:
  %t = trunc i32 %x to i1
  %r = sext i1 %t to i32
  ret i32 %r
}

define i32 @load_i1_sext(ptr %p) {
; CHECK-LABEL: load_i1_sext:
  %v = load i1, ptr %p
  %r = sext i1 %v to i32
  ret i32 %r
}
