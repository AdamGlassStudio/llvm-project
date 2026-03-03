; RUN: llc -mtriple=vax-unknown-netbsdelf < %s | FileCheck %s

; Test unordered FP comparisons — these use different condition code
; handling than ordered comparisons (SETUGT, SETUGE, etc.)
; VAX D_float has no NaN, so uno is always false and ord is always true.

define i32 @fcmp_uno_f64(double %a, double %b) {
; CHECK-LABEL: fcmp_uno_f64:
; VAX has no NaN → uno is always false
; CHECK: clrl
  %c = fcmp uno double %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

define i32 @fcmp_ord_f64(double %a, double %b) {
; CHECK-LABEL: fcmp_ord_f64:
; VAX has no NaN → ord is always true
; CHECK: movl $1
  %c = fcmp ord double %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

define i32 @fcmp_ult_f64(double %a, double %b) {
; CHECK-LABEL: fcmp_ult_f64:
; CHECK: cmpd
  %c = fcmp ult double %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

define i32 @fcmp_ule_f64(double %a, double %b) {
; CHECK-LABEL: fcmp_ule_f64:
; CHECK: cmpd
  %c = fcmp ule double %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

define i32 @fcmp_ugt_f64(double %a, double %b) {
; CHECK-LABEL: fcmp_ugt_f64:
; CHECK: cmpd
  %c = fcmp ugt double %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

define i32 @fcmp_uge_f64(double %a, double %b) {
; CHECK-LABEL: fcmp_uge_f64:
; CHECK: cmpd
  %c = fcmp uge double %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

define i32 @fcmp_one_f64(double %a, double %b) {
; CHECK-LABEL: fcmp_one_f64:
; CHECK: cmpd
  %c = fcmp one double %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; F_float versions — same NaN rules apply
define i32 @fcmp_uno_f32(float %a, float %b) {
; CHECK-LABEL: fcmp_uno_f32:
; CHECK: clrl
  %c = fcmp uno float %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

define i32 @fcmp_ugt_f32(float %a, float %b) {
; CHECK-LABEL: fcmp_ugt_f32:
; CHECK: cmpf
  %c = fcmp ugt float %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}
