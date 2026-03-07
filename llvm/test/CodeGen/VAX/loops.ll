; RUN: llc -mtriple=vax-unknown-netbsdelf < %s | FileCheck %s

; Test loop idioms — sum and count patterns

; Sum array elements
define i32 @sum_array(ptr %arr, i32 %n) {
; CHECK-LABEL: sum_array:
; CHECK: movl	8(%ap), %r1
; CHECK-NEXT: clrl	%r0
; CHECK-NEXT: cmpl	%r1, $1
; CHECK-NEXT: blss	{{.*}}
; CHECK: movl	4(%ap), %r2
; CHECK-NEXT: clrl	%r0
; CHECK-NEXT: {{.*}}:                                # %loop
; CHECK: addl3	(%r2), %r0, %r0
; CHECK-NEXT: decl	%r1
; CHECK-NEXT: addl2	$4, %r2
; CHECK-NEXT: tstl	%r1
; CHECK-NEXT: bneq	{{.*}}
; CHECK-NEXT: {{.*}}:                                # %exit
; CHECK-NEXT: ret
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
; CHECK: movl	8(%ap), %r1
; CHECK-NEXT: clrl	%r0
; CHECK-NEXT: cmpl	%r1, $1
; CHECK-NEXT: bgeq	{{.*}}
; CHECK-NEXT: brw	{{.*}}
; CHECK-NEXT: {{.*}}:                                # %loop.preheader
; CHECK-NEXT: movl	4(%ap), %r2
; CHECK-NEXT: clrl	%r3
; CHECK-NEXT: movl	$1, %r4
; CHECK-NEXT: movl	%r3, %r0
; CHECK-NEXT: brw	{{.*}}
; CHECK-NEXT: {{.*}}:                                # %loop
; CHECK: bicl2	$-2, %r5
; CHECK-NEXT: addl2	%r5, %r0
; CHECK-NEXT: decl	%r1
; CHECK-NEXT: addl2	$4, %r2
; CHECK-NEXT: tstl	%r1
; CHECK-NEXT: beql	{{.*}}
; CHECK-NEXT: {{.*}}:                                # %loop
; CHECK: cmpl	(%r2), %r3
; CHECK-NEXT: movl	%r4, %r5
; CHECK-NEXT: beql	{{.*}}
; CHECK-NEXT: brw	{{.*}}
; CHECK-NEXT: {{.*}}:                                # %loop
; CHECK: movl	%r3, %r5
; CHECK-NEXT: brw	{{.*}}
; CHECK-NEXT: {{.*}}:                                # %exit
; CHECK-NEXT: ret
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
