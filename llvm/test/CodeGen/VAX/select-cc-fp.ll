; RUN: llc -mtriple=vax-unknown-netbsdelf < %s | FileCheck %s
;
; Test SELECT_CC with f64 result type.
; Regression test: SELECT_CC_D_Pseudo was missing a DAG pattern.

define double @select_cc_f64(double %a, double %b, i32 %c) {
; CHECK-LABEL: select_cc_f64:
; CHECK: clrl	%r0
; CHECK-NEXT: cmpl	20(%ap), %r0
; CHECK-NEXT: bgtr	{{.*}}
; CHECK: addl3	$12, %ap, %r0
; CHECK-NEXT: movd	(%r0), %r0
; CHECK-NEXT: ret
; CHECK-NEXT: {{.*}}:
; CHECK-NEXT: addl3	$4, %ap, %r0
; CHECK-NEXT: movd	(%r0), %r0
; CHECK-NEXT: ret
  %cmp = icmp sgt i32 %c, 0
  %r = select i1 %cmp, double %a, double %b
  ret double %r
}

define float @select_cc_f32(float %a, float %b, i32 %c) {
; CHECK-LABEL: select_cc_f32:
; CHECK: clrl	%r0
; CHECK-NEXT: cmpl	12(%ap), %r0
; CHECK-NEXT: bgtr	{{.*}}
; CHECK: addl3	$8, %ap, %r0
; CHECK-NEXT: movf	(%r0), %r0
; CHECK-NEXT: ret
; CHECK-NEXT: {{.*}}:
; CHECK-NEXT: addl3	$4, %ap, %r0
; CHECK-NEXT: movf	(%r0), %r0
; CHECK-NEXT: ret
  %cmp = icmp sgt i32 %c, 0
  %r = select i1 %cmp, float %a, float %b
  ret float %r
}
