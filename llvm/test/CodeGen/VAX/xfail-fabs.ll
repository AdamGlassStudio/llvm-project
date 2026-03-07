; RUN: llc -mtriple=vax-unknown-netbsdelf < %s | FileCheck %s
;
; fabs f32: LLVM expands fabs to BICL of sign bit, using stack-based
; bitcast (f32 → i32 → clear sign bit → i32 → f32).

declare float @llvm.fabs.f32(float)

define float @fabs_f32(float %a) {
; CHECK-LABEL: fabs_f32:
; CHECK: subl2	$8, %sp
; CHECK-NEXT: movf	4(%ap), %r0
; CHECK-NEXT: movf	%r0, -8(%fp)
; CHECK-NEXT: movl	$-2147483648, %r0
; CHECK-NEXT: bicl3	%r0, -8(%fp), %r0
; CHECK-NEXT: movl	%r0, -4(%fp)
; CHECK-NEXT: movf	-4(%fp), %r0
; CHECK-NEXT: ret
; CHECK-NEXT: .Lfunc_end0:
  %r = call float @llvm.fabs.f32(float %a)
  ret float %r
}
