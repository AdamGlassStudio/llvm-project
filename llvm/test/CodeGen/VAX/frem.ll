; RUN: llc -march=vax -O2 < %s | FileCheck %s

; Verify that FREM lowers to fmodf/fmod libcalls, not the multi-step
; expansion that requires fmaf/fma (which VAX libm does not provide).

define float @test_fremf(float %a, float %b) {
; CHECK-LABEL: test_fremf:
; CHECK: calls $2, fmodf
; CHECK-NOT: fmaf
  %r = frem float %a, %b
  ret float %r
}

define double @test_fremd(double %a, double %b) {
; CHECK-LABEL: test_fremd:
; CHECK: calls $2, fmod
; CHECK-NOT: fma
  %r = frem double %a, %b
  ret double %r
}
