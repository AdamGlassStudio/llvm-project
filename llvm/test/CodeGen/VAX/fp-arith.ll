; RUN: llc -mtriple=vax-unknown-netbsdelf < %s | FileCheck %s

; Test F_float and D_float arithmetic

; F_float (32-bit) arithmetic
define float @fadd_f32(float %a, float %b) {
; CHECK-LABEL: fadd_f32:
; CHECK: addf2
  %r = fadd float %a, %b
  ret float %r
}

define float @fsub_f32(float %a, float %b) {
; CHECK-LABEL: fsub_f32:
; CHECK: subf2
  %r = fsub float %a, %b
  ret float %r
}

define float @fmul_f32(float %a, float %b) {
; CHECK-LABEL: fmul_f32:
; CHECK: mulf2
  %r = fmul float %a, %b
  ret float %r
}

define float @fdiv_f32(float %a, float %b) {
; CHECK-LABEL: fdiv_f32:
; CHECK: divf2
  %r = fdiv float %a, %b
  ret float %r
}

define float @fneg_f32(float %a) {
; CHECK-LABEL: fneg_f32:
; CHECK: mnegf
  %r = fneg float %a
  ret float %r
}

; D_float (64-bit) arithmetic
define double @fadd_f64(double %a, double %b) {
; CHECK-LABEL: fadd_f64:
; CHECK: addd2
  %r = fadd double %a, %b
  ret double %r
}

define double @fsub_f64(double %a, double %b) {
; CHECK-LABEL: fsub_f64:
; CHECK: subd2
  %r = fsub double %a, %b
  ret double %r
}

define double @fmul_f64(double %a, double %b) {
; CHECK-LABEL: fmul_f64:
; CHECK: muld2
  %r = fmul double %a, %b
  ret double %r
}

define double @fdiv_f64(double %a, double %b) {
; CHECK-LABEL: fdiv_f64:
; CHECK: divd2
  %r = fdiv double %a, %b
  ret double %r
}
