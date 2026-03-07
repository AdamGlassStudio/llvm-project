; RUN: llc -mtriple=vax-unknown-netbsdelf < %s | FileCheck %s
; Phase 34: FP completion — TSTF/TSTD, CLRF/CLRD patterns.

;--- TSTF: compare-to-zero branch ---

define i32 @tstf_branch(float %a) {
; CHECK-LABEL: tstf_branch:
; CHECK:       tstf %r0
; CHECK:       bleq
  %c = fcmp ogt float %a, 0.0
  br i1 %c, label %pos, label %neg
pos:
  ret i32 1
neg:
  ret i32 0
}

;--- TSTD: compare-to-zero branch ---

define i32 @tstd_branch(double %a) {
; CHECK-LABEL: tstd_branch:
; CHECK:       tstd %r0
; CHECK:       bgeq
  %c = fcmp olt double %a, 0.0
  br i1 %c, label %neg, label %pos
neg:
  ret i32 1
pos:
  ret i32 0
}

;--- TSTF: select_cc against zero ---

define float @tstf_select(float %a, float %x, float %y) {
; CHECK-LABEL: tstf_select:
; CHECK:       tstf
  %c = fcmp oeq float %a, 0.0
  %r = select i1 %c, float %x, float %y
  ret float %r
}

;--- TSTD: select_cc against zero ---

define double @tstd_select(double %a, double %x, double %y) {
; CHECK-LABEL: tstd_select:
; CHECK:       tstd
  %c = fcmp one double %a, 0.0
  %r = select i1 %c, double %x, double %y
  ret double %r
}

;--- Verify CMPF still used for non-zero comparisons ---

define i32 @cmpf_nonzero(float %a, float %b) {
; CHECK-LABEL: cmpf_nonzero:
; CHECK:       cmpf
; CHECK-NOT:   tstf
  %c = fcmp ogt float %a, %b
  br i1 %c, label %t, label %f
t:
  ret i32 1
f:
  ret i32 0
}

;--- Verify CMPD still used for non-zero comparisons ---

define i32 @cmpd_nonzero(double %a, double %b) {
; CHECK-LABEL: cmpd_nonzero:
; CHECK:       cmpd
; CHECK-NOT:   tstd
  %c = fcmp olt double %a, %b
  br i1 %c, label %t, label %f
t:
  ret i32 1
f:
  ret i32 0
}
