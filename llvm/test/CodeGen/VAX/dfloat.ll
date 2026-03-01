; RUN: llc -march=vax < %s | FileCheck %s

; D_float add
define double @test_addd(double %a, double %b) {
; CHECK-LABEL: test_addd:
; CHECK: addd3
  %r = fadd double %a, %b
  ret double %r
}

; D_float subtract
define double @test_subd(double %a, double %b) {
; CHECK-LABEL: test_subd:
; CHECK: subd3
  %r = fsub double %a, %b
  ret double %r
}

; D_float multiply
define double @test_muld(double %a, double %b) {
; CHECK-LABEL: test_muld:
; CHECK: muld3
  %r = fmul double %a, %b
  ret double %r
}

; D_float divide
define double @test_divd(double %a, double %b) {
; CHECK-LABEL: test_divd:
; CHECK: divd3
  %r = fdiv double %a, %b
  ret double %r
}

; D_float to int conversion
define i32 @test_cvtdl(double %d) {
; CHECK-LABEL: test_cvtdl:
; CHECK: cvtdl
  %r = fptosi double %d to i32
  ret i32 %r
}

; int to D_float conversion
define double @test_cvtld(i32 %i) {
; CHECK-LABEL: test_cvtld:
; CHECK: cvtld
  %r = sitofp i32 %i to double
  ret double %r
}

; D_float negate
define double @test_mnegd(double %d) {
; CHECK-LABEL: test_mnegd:
; CHECK: mnegd
  %r = fneg double %d
  ret double %r
}

; D_float to F_float (truncate)
define float @test_cvtdf(double %d) {
; CHECK-LABEL: test_cvtdf:
; CHECK: cvtdf
  %r = fptrunc double %d to float
  ret float %r
}

; F_float to D_float (extend)
define double @test_cvtfd(float %f) {
; CHECK-LABEL: test_cvtfd:
; CHECK: cvtfd
  %r = fpext float %f to double
  ret double %r
}
