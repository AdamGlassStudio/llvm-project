; RUN: llc -mtriple=vax-unknown-netbsdelf < %s | FileCheck %s

; Test unsigned and signed division/remainder

define i32 @udiv(i32 %a, i32 %b) {
; CHECK-LABEL: udiv:
; CHECK: calls $2, __udivsi3
  %r = udiv i32 %a, %b
  ret i32 %r
}

define i32 @urem(i32 %a, i32 %b) {
; CHECK-LABEL: urem:
; CHECK: calls $2, __umodsi3
  %r = urem i32 %a, %b
  ret i32 %r
}

define i32 @sdiv_by_const(i32 %a) {
; CHECK-LABEL: sdiv_by_const:
; CHECK: divl3
  %r = sdiv i32 %a, 7
  ret i32 %r
}

define i32 @srem(i32 %a, i32 %b) {
; CHECK-LABEL: srem:
; CHECK: divl3
; CHECK: mull3
; CHECK: subl3
  %r = srem i32 %a, %b
  ret i32 %r
}
