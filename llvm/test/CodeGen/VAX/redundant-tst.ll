; RUN: llc -mtriple=vax-unknown-netbsdelf -O2 < %s | FileCheck %s
;
; Test redundant TST elimination: when the preceding instruction already
; sets N/Z flags on the same register, the TST is eliminated.

; ADDL3 sets flags on result — TSTL is redundant.
define i32 @add_branch(i32 %a, i32 %b) {
; CHECK-LABEL: add_branch:
; CHECK: addl3
; CHECK-NOT: tstl
; CHECK-NEXT: beql
  %sum = add i32 %a, %b
  %cmp = icmp eq i32 %sum, 0
  br i1 %cmp, label %zero, label %nonzero
zero:
  ret i32 0
nonzero:
  ret i32 %sum
}

; MOVL sets flags on result — TSTL is redundant.
define i32 @load_branch(ptr %p) {
; CHECK-LABEL: load_branch:
; CHECK: movl (%r0), %r0
; CHECK-NOT: tstl
; CHECK-NEXT: beql
  %v = load i32, ptr %p
  %cmp = icmp eq i32 %v, 0
  br i1 %cmp, label %zero, label %nonzero
zero:
  ret i32 1
nonzero:
  ret i32 %v
}

; BICL3 sets flags on result — TSTL is redundant.
define i32 @and_branch(i32 %a, i32 %b) {
; CHECK-LABEL: and_branch:
; CHECK: bicl3
; CHECK-NOT: tstl
; CHECK-NEXT: beql
  %v = and i32 %a, %b
  %cmp = icmp eq i32 %v, 0
  br i1 %cmp, label %zero, label %nonzero
zero:
  ret i32 1
nonzero:
  ret i32 0
}

; SUBL3 sets flags — but comparison is slt (not eq/ne), so CMPL is used
; instead of TSTL. The CMPL should NOT be eliminated.
define i32 @sub_slt_branch(i32 %a, i32 %b) {
; CHECK-LABEL: sub_slt_branch:
; CHECK: subl3
; CHECK: cmpl
; CHECK: bleq
  %diff = sub i32 %a, %b
  %cmp = icmp slt i32 %diff, 0
  br i1 %cmp, label %neg, label %pos
neg:
  ret i32 -1
pos:
  ret i32 %diff
}
