; RUN: llc -mtriple=vax-unknown-netbsdelf < %s | FileCheck %s

; Test SELECT_CC driven by floating-point conditions.

define float @select_f32_olt(float %a, float %b, float %t, float %f) {
; CHECK-LABEL: select_f32_olt:
; CHECK: cmpf
  %c = fcmp olt float %a, %b
  %r = select i1 %c, float %t, float %f
  ret float %r
}

define double @select_f64_ogt(double %a, double %b, double %t, double %f) {
; CHECK-LABEL: select_f64_ogt:
; CHECK: cmpd
  %c = fcmp ogt double %a, %b
  %r = select i1 %c, double %t, double %f
  ret double %r
}

define i32 @select_i32_from_fcmp(float %a, float %b, i32 %t, i32 %f) {
; CHECK-LABEL: select_i32_from_fcmp:
; CHECK: cmpf
  %c = fcmp oeq float %a, %b
  %r = select i1 %c, i32 %t, i32 %f
  ret i32 %r
}

define double @select_f64_ueq(double %a, double %b, double %t, double %f) {
; CHECK-LABEL: select_f64_ueq:
; CHECK: cmpd
  %c = fcmp ueq double %a, %b
  %r = select i1 %c, double %t, double %f
  ret double %r
}
