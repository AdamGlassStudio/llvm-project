; RUN: llc -mtriple=vax-unknown-netbsdelf < %s | FileCheck %s
;
; Test 64-bit shifts using ASHQ instruction.

define i64 @shl64(i64 %a, i32 %n) {
; CHECK-LABEL: shl64:
; CHECK:       movl	12(%ap), %r2
; CHECK:       movl	8(%ap), %r4
; CHECK:       movl	4(%ap), %r3
; CHECK:       ashq	%r2, %r3, %r0
; CHECK:       ret
  %ext = zext i32 %n to i64
  %r = shl i64 %a, %ext
  ret i64 %r
}

define i64 @lshr64(i64 %a, i32 %n) {
; CHECK-LABEL: lshr64:
; CHECK:       movl	4(%ap), %r0
; CHECK:       movl	8(%ap), %r1
; CHECK:       movl	12(%ap), %r2
; CHECK:       pushl	%r2
; CHECK:       pushl	%r1
; CHECK:       pushl	%r0
; CHECK:       calls	$3, __lshrdi3
; CHECK:       ret
  %ext = zext i32 %n to i64
  %r = lshr i64 %a, %ext
  ret i64 %r
}

define i64 @ashr64(i64 %a, i32 %n) {
; CHECK-LABEL: ashr64:
; CHECK:       movl	8(%ap), %r3
; CHECK:       movl	4(%ap), %r2
; CHECK:       mnegl	12(%ap), %r4
; CHECK:       ashq	%r4, %r2, %r0
; CHECK:       ret
  %ext = zext i32 %n to i64
  %r = ashr i64 %a, %ext
  ret i64 %r
}
