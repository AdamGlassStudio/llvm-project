; RUN: llc -mtriple=vax-unknown-netbsdelf < %s | FileCheck %s

; Test integer comparison and setcc patterns

; Unsigned less-than
define i32 @ucmp_lt(i32 %a, i32 %b) {
; CHECK-LABEL: ucmp_lt:
; CHECK: movl	8(%ap), %r0
; CHECK-NEXT: cmpl	4(%ap), %r0
; CHECK-NEXT: blssu	{{.*}}
; CHECK: clrl	%r0
; CHECK-NEXT: bicl2	$-2, %r0
; CHECK-NEXT: ret
; CHECK-NEXT: {{.*}}:
; CHECK-NEXT: movl	$1, %r0
; CHECK-NEXT: bicl2	$-2, %r0
; CHECK-NEXT: ret
  %c = icmp ult i32 %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; Unsigned greater-than
define i32 @ucmp_gt(i32 %a, i32 %b) {
; CHECK-LABEL: ucmp_gt:
; CHECK: movl	8(%ap), %r0
; CHECK-NEXT: cmpl	4(%ap), %r0
; CHECK-NEXT: bgtru	{{.*}}
; CHECK: clrl	%r0
; CHECK-NEXT: bicl2	$-2, %r0
; CHECK-NEXT: ret
; CHECK-NEXT: {{.*}}:
; CHECK-NEXT: movl	$1, %r0
; CHECK-NEXT: bicl2	$-2, %r0
; CHECK-NEXT: ret
  %c = icmp ugt i32 %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; Signed equal
define i32 @scmp_eq(i32 %a, i32 %b) {
; CHECK-LABEL: scmp_eq:
; CHECK: movl	8(%ap), %r0
; CHECK-NEXT: cmpl	4(%ap), %r0
; CHECK-NEXT: beql	{{.*}}
; CHECK: clrl	%r0
; CHECK-NEXT: bicl2	$-2, %r0
; CHECK-NEXT: ret
; CHECK-NEXT: {{.*}}:
; CHECK-NEXT: movl	$1, %r0
; CHECK-NEXT: bicl2	$-2, %r0
; CHECK-NEXT: ret
  %c = icmp eq i32 %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; Signed not-equal
define i32 @scmp_ne(i32 %a, i32 %b) {
; CHECK-LABEL: scmp_ne:
; CHECK: movl	8(%ap), %r0
; CHECK-NEXT: cmpl	4(%ap), %r0
; CHECK-NEXT: bneq	{{.*}}
; CHECK: clrl	%r0
; CHECK-NEXT: bicl2	$-2, %r0
; CHECK-NEXT: ret
; CHECK-NEXT: {{.*}}:
; CHECK-NEXT: movl	$1, %r0
; CHECK-NEXT: bicl2	$-2, %r0
; CHECK-NEXT: ret
  %c = icmp ne i32 %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; Compare against zero — should use tstl
define i32 @cmp_zero(i32 %a) {
; CHECK-LABEL: cmp_zero:
; CHECK: clrl	%r0
; CHECK-NEXT: cmpl	4(%ap), %r0
; CHECK-NEXT: bneq	{{.*}}
; CHECK: movl	$1, %r0
; CHECK-NEXT: {{.*}}:
; CHECK-NEXT: bicl2	$-2, %r0
; CHECK-NEXT: ret
  %c = icmp eq i32 %a, 0
  %r = zext i1 %c to i32
  ret i32 %r
}

; Signed less-than
define i32 @scmp_slt(i32 %a, i32 %b) {
; CHECK-LABEL: scmp_slt:
; CHECK: movl	8(%ap), %r0
; CHECK-NEXT: cmpl	4(%ap), %r0
; CHECK-NEXT: blss	{{.*}}
; CHECK: clrl	%r0
; CHECK-NEXT: bicl2	$-2, %r0
; CHECK-NEXT: ret
; CHECK-NEXT: {{.*}}:
; CHECK-NEXT: movl	$1, %r0
; CHECK-NEXT: bicl2	$-2, %r0
; CHECK-NEXT: ret
  %c = icmp slt i32 %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; Signed greater-than
define i32 @scmp_sgt(i32 %a, i32 %b) {
; CHECK-LABEL: scmp_sgt:
; CHECK: movl	8(%ap), %r0
; CHECK-NEXT: cmpl	4(%ap), %r0
; CHECK-NEXT: bgtr	{{.*}}
; CHECK: clrl	%r0
; CHECK-NEXT: bicl2	$-2, %r0
; CHECK-NEXT: ret
; CHECK-NEXT: {{.*}}:
; CHECK-NEXT: movl	$1, %r0
; CHECK-NEXT: bicl2	$-2, %r0
; CHECK-NEXT: ret
  %c = icmp sgt i32 %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}
