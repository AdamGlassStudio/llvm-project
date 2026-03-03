; RUN: llc -mtriple=vax-unknown-netbsdelf < %s | FileCheck %s

; Test unsigned and signed division/remainder using EDIV and DIVL3.

define i32 @udiv(i32 %a, i32 %b) {
; CHECK-LABEL: udiv:
; CHECK: clrl
; CHECK: ediv
  %r = udiv i32 %a, %b
  ret i32 %r
}

define i32 @urem(i32 %a, i32 %b) {
; CHECK-LABEL: urem:
; CHECK: clrl
; CHECK: ediv
  %r = urem i32 %a, %b
  ret i32 %r
}

define i32 @sdiv_by_const(i32 %a) {
; CHECK-LABEL: sdiv_by_const:
; SMUL_LOHI available → magic-number multiply via EMUL.
; CHECK: emul
  %r = sdiv i32 %a, 7
  ret i32 %r
}

define i32 @sdiv(i32 %a, i32 %b) {
; CHECK-LABEL: sdiv:
; CHECK: divl3
  %r = sdiv i32 %a, %b
  ret i32 %r
}

define i32 @srem(i32 %a, i32 %b) {
; CHECK-LABEL: srem:
; Signed remainder via EDIV with sign-extended dividend.
; CHECK: ashl $-31
; CHECK: ediv
  %r = srem i32 %a, %b
  ret i32 %r
}

; Verify that udiv+urem of the same operands share a single EDIV.
define i32 @udivrem(i32 %a, i32 %b) {
; CHECK-LABEL: udivrem:
; CHECK: clrl
; CHECK: ediv
; CHECK-NOT: ediv
  %q = udiv i32 %a, %b
  %r = urem i32 %a, %b
  %s = add i32 %q, %r
  ret i32 %s
}
