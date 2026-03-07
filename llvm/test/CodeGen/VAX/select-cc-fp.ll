; RUN: llc -mtriple=vax-unknown-netbsdelf < %s | FileCheck %s
;
; Test SELECT_CC with f64 result type.
; Regression test: SELECT_CC_D_Pseudo was missing a DAG pattern.

define double @select_cc_f64(double %a, double %b, i32 %c) {
; CHECK-LABEL: select_cc_f64:
; CHECK:       tstl	20(%ap)
; CHECK:       bgtr	.LBB0_2
; CHECK:       addl3	$12, %ap, %r0
; CHECK:       movd	(%r0), %r0
; CHECK:       ret
; CHECK:       .LBB0_2:
; CHECK:       addl3	$4, %ap, %r0
; CHECK:       movd	(%r0), %r0
; CHECK:       ret
  %cmp = icmp sgt i32 %c, 0
  %r = select i1 %cmp, double %a, double %b
  ret double %r
}

define float @select_cc_f32(float %a, float %b, i32 %c) {
; CHECK-LABEL: select_cc_f32:
; CHECK:       tstl	12(%ap)
; CHECK:       bgtr	.LBB1_2
; CHECK:       addl3	$8, %ap, %r0
; CHECK:       movf	(%r0), %r0
; CHECK:       ret
; CHECK:       .LBB1_2:
; CHECK:       addl3	$4, %ap, %r0
; CHECK:       movf	(%r0), %r0
; CHECK:       ret
  %cmp = icmp sgt i32 %c, 0
  %r = select i1 %cmp, float %a, float %b
  ret float %r
}
