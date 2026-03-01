; RUN: llc -mtriple=vax-unknown-netbsdelf < %s | FileCheck %s
; XFAIL: *
;
; Bug: fabs f32 crashes — bitcast f32 ↔ i32 not selected.
; LLVM expands fabs to BICL of sign bit then bitcast back to f32.
; Fix: add bitcast patterns, or custom-lower ISD::FABS to a native
; operation on the float representation.

declare float @llvm.fabs.f32(float)

define float @fabs_f32(float %a) {
; CHECK-LABEL: fabs_f32:
; CHECK: ret
  %r = call float @llvm.fabs.f32(float %a)
  ret float %r
}
