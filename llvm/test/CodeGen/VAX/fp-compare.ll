; RUN: llc -mtriple=vax-unknown-netbsdelf < %s | FileCheck %s

; Test floating-point comparisons (both F_float and D_float)

define i32 @fcmp_oeq_f(float %a, float %b) {
; CHECK-LABEL: fcmp_oeq_f:
; CHECK: cmpf
  %c = fcmp oeq float %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

define i32 @fcmp_ogt_f(float %a, float %b) {
; CHECK-LABEL: fcmp_ogt_f:
; CHECK: cmpf
  %c = fcmp ogt float %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

define i32 @fcmp_olt_f(float %a, float %b) {
; CHECK-LABEL: fcmp_olt_f:
; CHECK: cmpf
  %c = fcmp olt float %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

define i32 @fcmp_oeq_d(double %a, double %b) {
; CHECK-LABEL: fcmp_oeq_d:
; CHECK: cmpd
  %c = fcmp oeq double %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

define i32 @fcmp_ogt_d(double %a, double %b) {
; CHECK-LABEL: fcmp_ogt_d:
; CHECK: cmpd
  %c = fcmp ogt double %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; Compare float against zero — should use tstf (not cmpf + constant pool)
define i32 @fcmp_zero_f(float %a) {
; CHECK-LABEL: fcmp_zero_f:
; CHECK: tstf
  %c = fcmp oeq float %a, 0.0
  %r = zext i1 %c to i32
  ret i32 %r
}

; Compare double against zero — should use tstd (not cmpd + constant pool)
define i32 @fcmp_zero_d(double %a) {
; CHECK-LABEL: fcmp_zero_d:
; CHECK: tstd
  %c = fcmp oeq double %a, 0.0
  %r = zext i1 %c to i32
  ret i32 %r
}

; Float select
define float @fselect(float %a, float %b, i32 %cond) {
; CHECK-LABEL: fselect:
  %c = icmp eq i32 %cond, 0
  %r = select i1 %c, float %a, float %b
  ret float %r
}
