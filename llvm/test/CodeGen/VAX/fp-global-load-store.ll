; RUN: llc -mtriple=vax-unknown-netbsdelf < %s | FileCheck %s

; Test floating-point global load and store patterns (MOVF/MOVD with globals).

@gf = external global float
@gd = external global double

define float @load_global_f32() {
; CHECK-LABEL: load_global_f32:
; CHECK: movl gf
  %v = load float, ptr @gf
  ret float %v
}

define void @store_global_f32(float %v) {
; CHECK-LABEL: store_global_f32:
; CHECK: movl {{.*}}, gf
  store float %v, ptr @gf
  ret void
}

define double @load_global_f64() {
; CHECK-LABEL: load_global_f64:
; CHECK: movd gd
  %v = load double, ptr @gd
  ret double %v
}

define void @store_global_f64(double %v) {
; CHECK-LABEL: store_global_f64:
; CHECK: movd {{.*}}, gd
  store double %v, ptr @gd
  ret void
}

define float @load_add_store_f32(float %x) {
; CHECK-LABEL: load_add_store_f32:
; CHECK: addf
  %v = load float, ptr @gf
  %r = fadd float %v, %x
  store float %r, ptr @gf
  ret float %r
}
