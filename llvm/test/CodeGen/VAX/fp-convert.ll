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

; Truncating f64→f32 store: should emit cvtdf + movf (f32 store)
define void @trunc_store_f32(ptr %p, double %d) {
; CHECK-LABEL: trunc_store_f32:
; CHECK: cvtdf
; CHECK: movf
  %t = fptrunc double %d to float
  store float %t, ptr %p
  ret void
}

; Truncating f64→f32 store to global
@gfloat = external global float
define void @trunc_store_f32_global(double %d) {
; CHECK-LABEL: trunc_store_f32_global:
; CHECK: cvtdf
; CHECK: movf
  %t = fptrunc double %d to float
  store float %t, ptr @gfloat
  ret void
}

; Extending load f32→f64: should emit movf (f32 load) + cvtfd
define double @ext_load_f32(ptr %p) {
; CHECK-LABEL: ext_load_f32:
; CHECK: movf
; CHECK: cvtfd
  %v = load float, ptr %p
  %r = fpext float %v to double
  ret double %r
}
