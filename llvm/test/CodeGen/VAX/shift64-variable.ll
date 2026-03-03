; RUN: llc -mtriple=vax-unknown-netbsdelf < %s | FileCheck %s

; Test variable-count 64-bit shifts (SHL_PARTS/SRL_PARTS/SRA_PARTS).

define i64 @shl_i64_var(i64 %a, i64 %b) {
; CHECK-LABEL: shl_i64_var:
; CHECK: ashq
  %r = shl i64 %a, %b
  ret i64 %r
}

define i64 @lshr_i64_var(i64 %a, i64 %b) {
; CHECK-LABEL: lshr_i64_var:
; CHECK: calls {{.*}}, __lshrdi3
  %r = lshr i64 %a, %b
  ret i64 %r
}

define i64 @ashr_i64_var(i64 %a, i64 %b) {
; CHECK-LABEL: ashr_i64_var:
  %r = ashr i64 %a, %b
  ret i64 %r
}

; Constant-count shift by 5 uses paired i32 shifts.
define i64 @shl_i64_const(i64 %a) {
; CHECK-LABEL: shl_i64_const:
; CHECK: ashl
  %r = shl i64 %a, 5
  ret i64 %r
}
