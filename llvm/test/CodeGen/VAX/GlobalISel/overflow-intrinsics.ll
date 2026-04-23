; RUN: llc -mtriple=vax-unknown-netbsdelf -global-isel -global-isel-abort=1 < %s | FileCheck %s

; G_UADDO / G_USUBO / G_UADDE / G_USUBE selectors, exercised directly
; via llvm.uadd.with.overflow / llvm.usub.with.overflow intrinsics
; rather than indirectly through i64 add/sub (which is covered in
; alu-i64.ll). These produce (result, carry-bit) pairs; selectAddOSubO
; / selectAddESubE lower them to ADDL3/SUBL3 followed by ADWC/SBWC or
; BCS tests.

declare {i32, i1} @llvm.uadd.with.overflow.i32(i32, i32)
declare {i32, i1} @llvm.usub.with.overflow.i32(i32, i32)

define i32 @gisel_uaddo(i32 %a, i32 %b) {
; CHECK-LABEL: gisel_uaddo:
; CHECK: addl
; CHECK: ret
  %o = call {i32, i1} @llvm.uadd.with.overflow.i32(i32 %a, i32 %b)
  %r = extractvalue {i32, i1} %o, 0
  ret i32 %r
}

define i32 @gisel_usubo(i32 %a, i32 %b) {
; CHECK-LABEL: gisel_usubo:
; CHECK: subl
; CHECK: ret
  %o = call {i32, i1} @llvm.usub.with.overflow.i32(i32 %a, i32 %b)
  %r = extractvalue {i32, i1} %o, 0
  ret i32 %r
}

; The overflow intrinsics select cleanly when only the result value is
; used. Materializing the carry bit itself (extractvalue ... , 1 →
; zext i1 to i32) currently hits an unlegalized G_ZEXT s1 → s32 and
; is not exercised here; that's a separate legalizer gap.
