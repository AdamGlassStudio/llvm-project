; RUN: llc -mtriple=vax-unknown-netbsdelf < %s | FileCheck %s

; Test unsigned comparisons and setcc patterns

define i32 @setcc_eq(i32 %a, i32 %b) {
; CHECK-LABEL: setcc_eq:
; CHECK:       clrl	%r0
; CHECK:       cmpl	4(%ap), 8(%ap)
; CHECK:       bneq	.LBB0_2
; CHECK:       movl	$1, %r0
; CHECK:       .LBB0_2:
; CHECK:       bicl2	$-2, %r0
; CHECK:       ret
  %c = icmp eq i32 %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

define i32 @setcc_ne(i32 %a, i32 %b) {
; CHECK-LABEL: setcc_ne:
; CHECK:       clrl	%r0
; CHECK:       cmpl	4(%ap), 8(%ap)
; CHECK:       beql	.LBB1_2
; CHECK:       movl	$1, %r0
; CHECK:       .LBB1_2:
; CHECK:       bicl2	$-2, %r0
; CHECK:       ret
  %c = icmp ne i32 %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

define i32 @setcc_ult(i32 %a, i32 %b) {
; CHECK-LABEL: setcc_ult:
; CHECK:       clrl	%r0
; CHECK:       cmpl	4(%ap), 8(%ap)
; CHECK:       bgequ	.LBB2_2
; CHECK:       movl	$1, %r0
; CHECK:       .LBB2_2:
; CHECK:       bicl2	$-2, %r0
; CHECK:       ret
  %c = icmp ult i32 %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

define i32 @setcc_uge(i32 %a, i32 %b) {
; CHECK-LABEL: setcc_uge:
; CHECK:       clrl	%r0
; CHECK:       cmpl	4(%ap), 8(%ap)
; CHECK:       blssu	.LBB3_2
; CHECK:       movl	$1, %r0
; CHECK:       .LBB3_2:
; CHECK:       bicl2	$-2, %r0
; CHECK:       ret
  %c = icmp uge i32 %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

define i32 @setcc_sgt(i32 %a, i32 %b) {
; CHECK-LABEL: setcc_sgt:
; CHECK:       clrl	%r0
; CHECK:       cmpl	4(%ap), 8(%ap)
; CHECK:       bleq	.LBB4_2
; CHECK:       movl	$1, %r0
; CHECK:       .LBB4_2:
; CHECK:       bicl2	$-2, %r0
; CHECK:       ret
  %c = icmp sgt i32 %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

define i32 @setcc_sle(i32 %a, i32 %b) {
; CHECK-LABEL: setcc_sle:
; CHECK:       clrl	%r0
; CHECK:       cmpl	4(%ap), 8(%ap)
; CHECK:       bgtr	.LBB5_2
; CHECK:       movl	$1, %r0
; CHECK:       .LBB5_2:
; CHECK:       bicl2	$-2, %r0
; CHECK:       ret
  %c = icmp sle i32 %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; Compare against zero should use tstl
define i32 @cmp_zero(i32 %a) {
; CHECK-LABEL: cmp_zero:
; CHECK:       clrl	%r0
; CHECK:       tstl	4(%ap)
; CHECK:       bneq	.LBB6_2
; CHECK:       movl	$1, %r0
; CHECK:       .LBB6_2:
; CHECK:       bicl2	$-2, %r0
; CHECK:       ret
  %c = icmp eq i32 %a, 0
  %r = zext i1 %c to i32
  ret i32 %r
}

; Select pattern (conditional move)
define i32 @select_i32(i32 %a, i32 %b, i32 %c) {
; CHECK-LABEL: select_i32:
; CHECK:       moval	12(%ap), %r0
; CHECK:       moval	8(%ap), %r1
; CHECK:       tstl	4(%ap)
; CHECK:       bneq	.LBB7_2
; CHECK:       movl	%r1, %r0
; CHECK:       .LBB7_2:
; CHECK:       movl	(%r0), %r0
; CHECK:       ret
  %cond = icmp eq i32 %a, 0
  %r = select i1 %cond, i32 %b, i32 %c
  ret i32 %r
}
