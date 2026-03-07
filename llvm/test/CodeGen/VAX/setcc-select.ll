; RUN: llc -mtriple=vax-unknown-netbsdelf < %s | FileCheck %s

; Test unsigned comparisons and setcc patterns

define i32 @setcc_eq(i32 %a, i32 %b) {
; CHECK-LABEL: setcc_eq:
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

define i32 @setcc_ne(i32 %a, i32 %b) {
; CHECK-LABEL: setcc_ne:
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

define i32 @setcc_ult(i32 %a, i32 %b) {
; CHECK-LABEL: setcc_ult:
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

define i32 @setcc_uge(i32 %a, i32 %b) {
; CHECK-LABEL: setcc_uge:
; CHECK: movl	8(%ap), %r0
; CHECK-NEXT: cmpl	4(%ap), %r0
; CHECK-NEXT: bgequ	{{.*}}
; CHECK: clrl	%r0
; CHECK-NEXT: bicl2	$-2, %r0
; CHECK-NEXT: ret
; CHECK-NEXT: {{.*}}:
; CHECK-NEXT: movl	$1, %r0
; CHECK-NEXT: bicl2	$-2, %r0
; CHECK-NEXT: ret
  %c = icmp uge i32 %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

define i32 @setcc_sgt(i32 %a, i32 %b) {
; CHECK-LABEL: setcc_sgt:
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

define i32 @setcc_sle(i32 %a, i32 %b) {
; CHECK-LABEL: setcc_sle:
; CHECK: movl	8(%ap), %r0
; CHECK-NEXT: cmpl	4(%ap), %r0
; CHECK-NEXT: bleq	{{.*}}
; CHECK: clrl	%r0
; CHECK-NEXT: bicl2	$-2, %r0
; CHECK-NEXT: ret
; CHECK-NEXT: {{.*}}:
; CHECK-NEXT: movl	$1, %r0
; CHECK-NEXT: bicl2	$-2, %r0
; CHECK-NEXT: ret
  %c = icmp sle i32 %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; Compare against zero should use tstl
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

; Select pattern (conditional move)
define i32 @select_i32(i32 %a, i32 %b, i32 %c) {
; CHECK-LABEL: select_i32:
; CHECK: clrl	%r0
; CHECK-NEXT: cmpl	4(%ap), %r0
; CHECK-NEXT: beql	{{.*}}
; CHECK: addl3	$12, %ap, %r0
; CHECK-NEXT: movl	(%r0), %r0
; CHECK-NEXT: ret
; CHECK-NEXT: {{.*}}:
; CHECK-NEXT: addl3	$8, %ap, %r0
; CHECK-NEXT: movl	(%r0), %r0
; CHECK-NEXT: ret
  %cond = icmp eq i32 %a, 0
  %r = select i1 %cond, i32 %b, i32 %c
  ret i32 %r
}
