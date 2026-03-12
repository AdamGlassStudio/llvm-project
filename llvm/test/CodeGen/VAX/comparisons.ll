; RUN: llc -mtriple=vax-unknown-netbsdelf < %s | FileCheck %s

; Test integer comparison and setcc patterns

; Unsigned less-than
define i32 @ucmp_lt(i32 %a, i32 %b) {
; CHECK-LABEL: ucmp_lt:
; CHECK:       cmpl	4(%ap), 8(%ap)
; CHECK:       blssu	.LBB0_2
; CHECK:       clrl	%r0
; CHECK:       bicl2	$-2, %r0
; CHECK:       ret
  %c = icmp ult i32 %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; Unsigned greater-than
define i32 @ucmp_gt(i32 %a, i32 %b) {
; CHECK-LABEL: ucmp_gt:
; CHECK:       cmpl	4(%ap), 8(%ap)
; CHECK:       bgtru	.LBB1_2
; CHECK:       clrl	%r0
; CHECK:       bicl2	$-2, %r0
; CHECK:       ret
  %c = icmp ugt i32 %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; Signed equal
define i32 @scmp_eq(i32 %a, i32 %b) {
; CHECK-LABEL: scmp_eq:
; CHECK:       cmpl	4(%ap), 8(%ap)
; CHECK:       beql	.LBB2_2
; CHECK:       clrl	%r0
; CHECK:       bicl2	$-2, %r0
; CHECK:       ret
  %c = icmp eq i32 %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; Signed not-equal
define i32 @scmp_ne(i32 %a, i32 %b) {
; CHECK-LABEL: scmp_ne:
; CHECK:       cmpl	4(%ap), 8(%ap)
; CHECK:       bneq	.LBB3_2
; CHECK:       clrl	%r0
; CHECK:       bicl2	$-2, %r0
; CHECK:       ret
  %c = icmp ne i32 %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; Compare against zero — should use tstl
define i32 @cmp_zero(i32 %a) {
; CHECK-LABEL: cmp_zero:
; CHECK:       movl	$1, %r0
; CHECK:       tstl	4(%ap)
; CHECK:       beql	.LBB4_2
; CHECK:       clrl	%r0
; CHECK:       .LBB4_2:
; CHECK:       bicl2	$-2, %r0
; CHECK:       ret
  %c = icmp eq i32 %a, 0
  %r = zext i1 %c to i32
  ret i32 %r
}

; Signed less-than
define i32 @scmp_slt(i32 %a, i32 %b) {
; CHECK-LABEL: scmp_slt:
; CHECK:       cmpl	4(%ap), 8(%ap)
; CHECK:       blss	.LBB5_2
; CHECK:       clrl	%r0
; CHECK:       bicl2	$-2, %r0
; CHECK:       ret
  %c = icmp slt i32 %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; Signed greater-than
define i32 @scmp_sgt(i32 %a, i32 %b) {
; CHECK-LABEL: scmp_sgt:
; CHECK:       cmpl	4(%ap), 8(%ap)
; CHECK:       bgtr	.LBB6_2
; CHECK:       clrl	%r0
; CHECK:       bicl2	$-2, %r0
; CHECK:       ret
  %c = icmp sgt i32 %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}
