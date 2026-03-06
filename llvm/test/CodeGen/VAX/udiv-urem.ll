; RUN: llc -mtriple=vax-unknown-netbsdelf < %s | FileCheck %s
;
; Regression tests for unsigned division/remainder on VAX.
;
; VAX EDIV is a signed divide. When the divisor has its MSB set (>= 2^31),
; EDIV treats it as negative and produces wrong results. The backend must
; split unsigned division into two cases:
;   Case 1: divisor >= 2^31 → quotient is 0 or 1 (unsigned compare)
;   Case 2: divisor < 2^31 → EDIV with zero-extended 64-bit dividend
;
; Also serves as a regression test for the DAGCombiner infinite loop:
; emitting ISD::SDIV from Custom ISD::UDIV lowering caused an infinite
; UDIV→SDIV→UDIV cycle when dividing by a positive constant.

; Variable divisor: must emit both the MSB-set compare path and EDIV path.
define i32 @udiv_var(i32 %a, i32 %b) {
; CHECK-LABEL: udiv_var:
; CHECK:       ediv
; CHECK:       blss
  %r = udiv i32 %a, %b
  ret i32 %r
}

; Divisor with MSB set (0xFFFFFFFF = -1 signed): no EDIV needed,
; quotient is 0 or 1 depending on unsigned compare.
define i32 @udiv_large_const(i32 %a) {
; CHECK-LABEL: udiv_large_const:
; CHECK-NOT:   ediv
; CHECK:       cmpl
  %r = udiv i32 %a, -1
  ret i32 %r
}

; Divisor = 0x80000001 (MSB set): also no EDIV.
define i32 @udiv_msb_const(i32 %a) {
; CHECK-LABEL: udiv_msb_const:
; CHECK-NOT:   ediv
; CHECK:       cmpl
  %r = udiv i32 %a, 2147483649
  ret i32 %r
}

; Small positive constant divisor: must use EDIV and must NOT hang
; (regression for DAGCombiner infinite loop).
define i32 @udiv_small_const(i32 %a) {
; CHECK-LABEL: udiv_small_const:
; CHECK:       ediv
  %r = udiv i32 %a, 9
  ret i32 %r
}

; Variable divisor urem: both paths.
define i32 @urem_var(i32 %a, i32 %b) {
; CHECK-LABEL: urem_var:
; CHECK:       ediv
; CHECK:       blss
  %r = urem i32 %a, %b
  ret i32 %r
}

; Large constant divisor urem: no EDIV.
define i32 @urem_large_const(i32 %a) {
; CHECK-LABEL: urem_large_const:
; CHECK-NOT:   ediv
; CHECK:       cmpl
  %r = urem i32 %a, -1
  ret i32 %r
}

; Small constant divisor urem: must use EDIV (DAGCombiner regression).
define i32 @urem_small_const(i32 %a) {
; CHECK-LABEL: urem_small_const:
; CHECK:       ediv
  %r = urem i32 %a, 10
  ret i32 %r
}
