; RUN: llc -mtriple=vax-unknown-netbsdelf < %s | FileCheck %s
;
; fabs f32: LLVM expands fabs to BICL of sign bit, using stack-based
; bitcast (f32 → i32 → clear sign bit → i32 → f32).

declare float @llvm.fabs.f32(float)

define float @fabs_f32(float %a) {
; CHECK-LABEL: fabs_f32:
; CHECK: bicl3 $-2147483648
; CHECK: ret
  %r = call float @llvm.fabs.f32(float %a)
  ret float %r
}
