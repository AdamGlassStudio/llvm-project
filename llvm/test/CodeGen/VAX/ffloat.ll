; RUN: llc -march=vax < %s | FileCheck %s

; F_float add
define float @test_addf(float %a, float %b) {
; CHECK-LABEL: test_addf:
; CHECK: addf3
  %r = fadd float %a, %b
  ret float %r
}

; F_float subtract
define float @test_subf(float %a, float %b) {
; CHECK-LABEL: test_subf:
; CHECK: subf3
  %r = fsub float %a, %b
  ret float %r
}

; F_float multiply
define float @test_mulf(float %a, float %b) {
; CHECK-LABEL: test_mulf:
; CHECK: mulf3
  %r = fmul float %a, %b
  ret float %r
}

; F_float divide
define float @test_divf(float %a, float %b) {
; CHECK-LABEL: test_divf:
; CHECK: divf3
  %r = fdiv float %a, %b
  ret float %r
}

; F_float to int conversion
define i32 @test_cvtfl(float %f) {
; CHECK-LABEL: test_cvtfl:
; CHECK: cvtfl
  %r = fptosi float %f to i32
  ret i32 %r
}

; int to F_float conversion
define float @test_cvtlf(i32 %i) {
; CHECK-LABEL: test_cvtlf:
; CHECK: cvtlf
  %r = sitofp i32 %i to float
  ret float %r
}

; F_float negate
define float @test_mnegf(float %f) {
; CHECK-LABEL: test_mnegf:
; CHECK: mnegf
  %r = fneg float %f
  ret float %r
}
