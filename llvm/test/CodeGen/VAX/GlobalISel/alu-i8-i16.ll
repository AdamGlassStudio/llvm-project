; RUN: llc -mtriple=vax-unknown-netbsdelf -global-isel -global-isel-abort=1 < %s | FileCheck %s

; i8 and i16 arithmetic select to ADDB3/W3, SUBB3/W3, etc. via the
; selectALU B/W/L switch. i32 is covered by alu-i32.ll; this file
; exercises the narrower variants that the legalizer does NOT widen.

define i8 @gisel_add_i8(i8 %a, i8 %b) {
; CHECK-LABEL: gisel_add_i8:
; CHECK: addb
; CHECK: ret
  %r = add i8 %a, %b
  ret i8 %r
}

define i8 @gisel_sub_i8(i8 %a, i8 %b) {
; CHECK-LABEL: gisel_sub_i8:
; CHECK: subb
; CHECK: ret
  %r = sub i8 %a, %b
  ret i8 %r
}

define i16 @gisel_add_i16(i16 %a, i16 %b) {
; CHECK-LABEL: gisel_add_i16:
; CHECK: addw
; CHECK: ret
  %r = add i16 %a, %b
  ret i16 %r
}

define i16 @gisel_sub_i16(i16 %a, i16 %b) {
; CHECK-LABEL: gisel_sub_i16:
; CHECK: subw
; CHECK: ret
  %r = sub i16 %a, %b
  ret i16 %r
}

; Note: i8/i16 G_MUL currently has no selector (MULB/MULW TableGen
; patterns aren't wired into GISel yet); it falls out as "cannot
; select" under abort=1. Not tested here until the selector supports
; narrower multiplies.

; i1 boolean return via zext-to-i32 semantics.
define i32 @gisel_icmp_zext(i32 %a, i32 %b) {
; CHECK-LABEL: gisel_icmp_zext:
; CHECK: cmpl
; CHECK: ret
  %c = icmp ult i32 %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; G_TRUNC: i32 -> i8/i16.
define i8 @gisel_trunc_i32_i8(i32 %a) {
; CHECK-LABEL: gisel_trunc_i32_i8:
; CHECK: ret
  %r = trunc i32 %a to i8
  ret i8 %r
}

define i16 @gisel_trunc_i32_i16(i32 %a) {
; CHECK-LABEL: gisel_trunc_i32_i16:
; CHECK: ret
  %r = trunc i32 %a to i16
  ret i16 %r
}
