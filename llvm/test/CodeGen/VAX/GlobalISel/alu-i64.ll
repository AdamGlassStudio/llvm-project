; RUN: llc -mtriple=vax-unknown-netbsdelf -global-isel -global-isel-abort=1 < %s | FileCheck %s

; i64 ADD/SUB go through the QPRB register bank with ADWC/SBWC carry
; chain (added in the qpr+adwc commit).
define i64 @gisel_i64_add(i64 %a, i64 %b) {
; CHECK-LABEL: gisel_i64_add:
; CHECK: addl
; CHECK: adwc
; CHECK: ret
  %r = add i64 %a, %b
  ret i64 %r
}

define i64 @gisel_i64_sub(i64 %a, i64 %b) {
; CHECK-LABEL: gisel_i64_sub:
; CHECK: subl
; CHECK: sbwc
; CHECK: ret
  %r = sub i64 %a, %b
  ret i64 %r
}

; i64 AND/OR/XOR narrow to two 32-bit ops.
define i64 @gisel_i64_and(i64 %a, i64 %b) {
; CHECK-LABEL: gisel_i64_and:
; CHECK: bicl
; CHECK: bicl
; CHECK: ret
  %r = and i64 %a, %b
  ret i64 %r
}

define i64 @gisel_i64_or(i64 %a, i64 %b) {
; CHECK-LABEL: gisel_i64_or:
; CHECK: bisl
; CHECK: bisl
; CHECK: ret
  %r = or i64 %a, %b
  ret i64 %r
}

define i64 @gisel_i64_xor(i64 %a, i64 %b) {
; CHECK-LABEL: gisel_i64_xor:
; CHECK: xorl
; CHECK: xorl
; CHECK: ret
  %r = xor i64 %a, %b
  ret i64 %r
}

; i64 shl: ASHQ instruction (selected via selectShl64).
@S = external global i32
@A = external global i64

define i64 @gisel_i64_shl() {
; CHECK-LABEL: gisel_i64_shl:
; CHECK: ashq
; CHECK: ret
  %s = load i32, ptr @S
  %a = load i64, ptr @A
  %c = zext i32 %s to i64
  %r = shl i64 %a, %c
  ret i64 %r
}

; i64 ashr: MNEGL + ASHQ (selected via selectAshr64).
define i64 @gisel_i64_ashr() {
; CHECK-LABEL: gisel_i64_ashr:
; CHECK: mnegl
; CHECK: ashq
; CHECK: ret
  %s = load i32, ptr @S
  %a = load i64, ptr @A
  %c = zext i32 %s to i64
  %r = ashr i64 %a, %c
  ret i64 %r
}

; i64 mul/div/mod go through GISel libcalls.
define i64 @gisel_i64_mul(i64 %a, i64 %b) {
; CHECK-LABEL: gisel_i64_mul:
; CHECK: calls {{.*}}__muldi3
; CHECK: ret
  %r = mul i64 %a, %b
  ret i64 %r
}

define i64 @gisel_i64_udiv(i64 %a, i64 %b) {
; CHECK-LABEL: gisel_i64_udiv:
; CHECK: calls {{.*}}__udivdi3
; CHECK: ret
  %r = udiv i64 %a, %b
  ret i64 %r
}

define i64 @gisel_i64_sdiv(i64 %a, i64 %b) {
; CHECK-LABEL: gisel_i64_sdiv:
; CHECK: calls {{.*}}__divdi3
; CHECK: ret
  %r = sdiv i64 %a, %b
  ret i64 %r
}

define i64 @gisel_i64_urem(i64 %a, i64 %b) {
; CHECK-LABEL: gisel_i64_urem:
; CHECK: calls {{.*}}__umoddi3
; CHECK: ret
  %r = urem i64 %a, %b
  ret i64 %r
}

; i64 return value: two halves assigned to R0:R1 by RetCC_VAX.
define i64 @gisel_i64_const() {
; CHECK-LABEL: gisel_i64_const:
; CHECK-DAG: %r0
; CHECK-DAG: %r1
; CHECK: ret
  ret i64 4294967297
}
