; RUN: llc -mtriple=vax-unknown-netbsdelf -global-isel -global-isel-abort=1 < %s | FileCheck %s

; Exercise the GISel path for 32-bit ALU operations. -global-isel-abort=1
; causes the compiler to error rather than silently fall back to SDAG,
; so these tests are load-bearing for the GISel selector + legalizer.

define i32 @gisel_add(i32 %a, i32 %b) {
; CHECK-LABEL: gisel_add:
; CHECK: addl
; CHECK: ret
  %r = add i32 %a, %b
  ret i32 %r
}

define i32 @gisel_sub(i32 %a, i32 %b) {
; CHECK-LABEL: gisel_sub:
; CHECK: subl
; CHECK: ret
  %r = sub i32 %a, %b
  ret i32 %r
}

define i32 @gisel_and(i32 %a, i32 %b) {
; CHECK-LABEL: gisel_and:
; CHECK: bicl
; CHECK: ret
  %r = and i32 %a, %b
  ret i32 %r
}

define i32 @gisel_or(i32 %a, i32 %b) {
; CHECK-LABEL: gisel_or:
; CHECK: bisl
; CHECK: ret
  %r = or i32 %a, %b
  ret i32 %r
}

define i32 @gisel_xor(i32 %a, i32 %b) {
; CHECK-LABEL: gisel_xor:
; CHECK: xorl
; CHECK: ret
  %r = xor i32 %a, %b
  ret i32 %r
}

define i32 @gisel_mul(i32 %a, i32 %b) {
; CHECK-LABEL: gisel_mul:
; CHECK: mull
; CHECK: ret
  %r = mul i32 %a, %b
  ret i32 %r
}

define i32 @gisel_sdiv(i32 %a, i32 %b) {
; CHECK-LABEL: gisel_sdiv:
; CHECK: divl
; CHECK: ret
  %r = sdiv i32 %a, %b
  ret i32 %r
}

define i32 @gisel_shl(i32 %a, i32 %b) {
; CHECK-LABEL: gisel_shl:
; CHECK: ashl
; CHECK: ret
  %r = shl i32 %a, %b
  ret i32 %r
}

define i32 @gisel_ashr(i32 %a, i32 %b) {
; CHECK-LABEL: gisel_ashr:
; CHECK: mnegl
; CHECK: ashl
; CHECK: ret
  %r = ashr i32 %a, %b
  ret i32 %r
}
