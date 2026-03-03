; RUN: llc -mtriple=vax-unknown-netbsdelf < %s | FileCheck %s

; Test FMA intrinsic expansion (VAX has no FMA instruction).

declare float @llvm.fma.f32(float, float, float)
declare double @llvm.fma.f64(double, double, double)

define float @fma_f32(float %a, float %b, float %c) {
; CHECK-LABEL: fma_f32:
; CHECK: calls
  %r = call float @llvm.fma.f32(float %a, float %b, float %c)
  ret float %r
}

define double @fma_f64(double %a, double %b, double %c) {
; CHECK-LABEL: fma_f64:
; CHECK: calls
  %r = call double @llvm.fma.f64(double %a, double %b, double %c)
  ret double %r
}
