; RUN: llc -mtriple=vax-unknown-netbsdelf < %s | FileCheck %s

; Test overflow intrinsics and select patterns

declare { i32, i1 } @llvm.sadd.with.overflow.i32(i32, i32)

; Signed add overflow — extract value
define i32 @sadd_overflow(i32 %a, i32 %b) {
; CHECK-LABEL: sadd_overflow:
; CHECK: movl	8(%ap), %r0
; CHECK-NEXT: addl2	4(%ap), %r0
; CHECK-NEXT: ret
  %r = call { i32, i1 } @llvm.sadd.with.overflow.i32(i32 %a, i32 %b)
  %v = extractvalue { i32, i1 } %r, 0
  ret i32 %v
}

; Absolute value pattern
define i32 @abs_val(i32 %a) {
; CHECK-LABEL: abs_val:
; CHECK: movl	4(%ap), %r0
; CHECK-NEXT: ashl	$-31, %r0, %r1
; CHECK-NEXT: xorl2	%r1, %r0
; CHECK-NEXT: subl2	%r1, %r0
; CHECK-NEXT: ret
  %neg = sub i32 0, %a
  %cmp = icmp sgt i32 %a, -1
  %r = select i1 %cmp, i32 %a, i32 %neg
  ret i32 %r
}

; Signed minimum
define i32 @min_i32(i32 %a, i32 %b) {
; CHECK-LABEL: min_i32:
; CHECK: movl	8(%ap), %r0
; CHECK-NEXT: movl	4(%ap), %r1
; CHECK-NEXT: cmpl	%r1, %r0
; CHECK-NEXT: bgeq	.LBB2_2
; CHECK: movl	%r1, %r0
; CHECK-NEXT: .LBB2_2:
; CHECK-NEXT: ret
  %c = icmp slt i32 %a, %b
  %r = select i1 %c, i32 %a, i32 %b
  ret i32 %r
}

; Signed maximum
define i32 @max_i32(i32 %a, i32 %b) {
; CHECK-LABEL: max_i32:
; CHECK: movl	8(%ap), %r0
; CHECK-NEXT: movl	4(%ap), %r1
; CHECK-NEXT: cmpl	%r1, %r0
; CHECK-NEXT: bleq	.LBB3_2
; CHECK: movl	%r1, %r0
; CHECK-NEXT: .LBB3_2:
; CHECK-NEXT: ret
  %c = icmp sgt i32 %a, %b
  %r = select i1 %c, i32 %a, i32 %b
  ret i32 %r
}

; Clamp to range [lo, hi]
define i32 @clamp(i32 %x, i32 %lo, i32 %hi) {
; CHECK-LABEL: clamp:
; CHECK: movq	4(%ap), %r0
; CHECK-NEXT: cmpl	%r0, %r1
; CHECK-NEXT: bgeq	.LBB4_2
; CHECK: movl	%r1, %r0
; CHECK: .LBB4_2:
; CHECK-NEXT: movl	12(%ap), %r1
; CHECK-NEXT: cmpl	%r0, %r1
; CHECK-NEXT: bleq	.LBB4_4
; CHECK: movl	%r1, %r0
; CHECK: .LBB4_4:
; CHECK-NEXT: ret
  %c1 = icmp slt i32 %x, %lo
  %v1 = select i1 %c1, i32 %lo, i32 %x
  %c2 = icmp sgt i32 %v1, %hi
  %r = select i1 %c2, i32 %hi, i32 %v1
  ret i32 %r
}
