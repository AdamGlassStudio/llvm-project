; RUN: llc -mtriple=vax-unknown-netbsdelf < %s | FileCheck %s

; Test loop idioms — sum and count patterns

; Sum array elements
define i32 @sum_array(ptr %arr, i32 %n) {
; CHECK-LABEL: sum_array:
; CHECK:       clrl	%r0
; CHECK:       movl	8(%ap), %r1
; CHECK:       cmpl	%r1, $1
; CHECK:       blss	.LBB0_3
; CHECK:       movl	4(%ap), %r2
; CHECK:       clrl	%r0
; CHECK:       .LBB0_2:
; CHECK:       addl2	(%r2), %r0
; CHECK:       addl2	$4, %r2
; CHECK:       decl	%r1
; CHECK:       bneq	.LBB0_2
; CHECK:       .LBB0_3:
; CHECK:       ret
entry:
  %cmp = icmp sgt i32 %n, 0
  br i1 %cmp, label %loop, label %exit
loop:
  %i = phi i32 [0, %entry], [%i.next, %loop]
  %sum = phi i32 [0, %entry], [%sum.next, %loop]
  %gep = getelementptr i32, ptr %arr, i32 %i
  %v = load i32, ptr %gep
  %sum.next = add i32 %sum, %v
  %i.next = add i32 %i, 1
  %done = icmp eq i32 %i.next, %n
  br i1 %done, label %exit, label %loop
exit:
  %result = phi i32 [0, %entry], [%sum.next, %loop]
  ret i32 %result
}

; Count nonzero elements
define i32 @count_nonzero(ptr %arr, i32 %n) {
; CHECK-LABEL: count_nonzero:
; CHECK:       clrl	%r0
; CHECK:       movl	8(%ap), %r1
; CHECK:       cmpl	%r1, $1
; CHECK:       blss	.LBB1_5
; CHECK:       movl	4(%ap), %r2
; CHECK:       clrl	%r3
; Rematerialization: $1 is recomputed in-loop instead of hoisted + copied.
; CHECK:       movl	%r3, %r0
; CHECK:       brb	.LBB1_3
; CHECK:       .LBB1_2:
; CHECK:       bicl2	$-2, %r4
; CHECK:       addl2	%r4, %r0
; CHECK:       addl2	$4, %r2
; CHECK:       decl	%r1
; CHECK:       beql	.LBB1_5
; CHECK:       .LBB1_3:
; CHECK:       movl	$1, %r4
; CHECK:       tstl	(%r2)
; CHECK:       bneq	.LBB1_2
; CHECK:       movl	%r3, %r4
; CHECK:       brb	.LBB1_2
; CHECK:       .LBB1_5:
; CHECK:       ret
entry:
  %cmp = icmp sgt i32 %n, 0
  br i1 %cmp, label %loop, label %exit
loop:
  %i = phi i32 [0, %entry], [%i.next, %loop]
  %cnt = phi i32 [0, %entry], [%cnt.next, %loop]
  %gep = getelementptr i32, ptr %arr, i32 %i
  %v = load i32, ptr %gep
  %nz = icmp ne i32 %v, 0
  %inc = zext i1 %nz to i32
  %cnt.next = add i32 %cnt, %inc
  %i.next = add i32 %i, 1
  %done = icmp eq i32 %i.next, %n
  br i1 %done, label %exit, label %loop
exit:
  %result = phi i32 [0, %entry], [%cnt.next, %loop]
  ret i32 %result
}
