; RUN: llc -mtriple=vax-unknown-netbsdelf < %s | FileCheck %s

; Test floating-point conversions between types

; int to float
define float @cvt_i32_to_f32(i32 %a) {
; CHECK-LABEL: cvt_i32_to_f32:
; CHECK: cvtlf
  %r = sitofp i32 %a to float
  ret float %r
}

; float to int
define i32 @cvt_f32_to_i32(float %a) {
; CHECK-LABEL: cvt_f32_to_i32:
; CHECK: cvtfl
  %r = fptosi float %a to i32
  ret i32 %r
}

; int to double
define double @cvt_i32_to_f64(i32 %a) {
; CHECK-LABEL: cvt_i32_to_f64:
; CHECK: cvtld
  %r = sitofp i32 %a to double
  ret double %r
}

; double to int
define i32 @cvt_f64_to_i32(double %a) {
; CHECK-LABEL: cvt_f64_to_i32:
; CHECK: cvtdl
  %r = fptosi double %a to i32
  ret i32 %r
}

; float to double
define double @cvt_f32_to_f64(float %a) {
; CHECK-LABEL: cvt_f32_to_f64:
; CHECK: cvtfd
  %r = fpext float %a to double
  ret double %r
}

; double to float
define float @cvt_f64_to_f32(double %a) {
; CHECK-LABEL: cvt_f64_to_f32:
; CHECK: cvtdf
  %r = fptrunc double %a to float
  ret float %r
}
