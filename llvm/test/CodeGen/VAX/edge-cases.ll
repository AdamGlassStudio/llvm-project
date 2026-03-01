; RUN: llc -mtriple=vax-unknown-netbsdelf < %s | FileCheck %s

; Test edge cases and unusual IR patterns

; Unreachable code
define i32 @with_unreachable(i32 %a) {
; CHECK-LABEL: with_unreachable:
entry:
  %cond = icmp eq i32 %a, 0
  br i1 %cond, label %zero, label %nonzero

zero:
  ret i32 0

nonzero:
  ret i32 %a
}

; Single-block function with many operations
define i32 @single_block(i32 %a, i32 %b) {
; CHECK-LABEL: single_block:
; CHECK: ret
  %r1 = add i32 %a, %b
  %r2 = mul i32 %r1, %a
  %r3 = sub i32 %r2, %b
  %r4 = and i32 %r3, 255
  ret i32 %r4
}

; Function with no arguments and no locals
define i32 @trivial() {
; CHECK-LABEL: trivial:
; CHECK: movl $42, %r0
; CHECK: ret
  ret i32 42
}

; Phi node with multiple predecessors
define i32 @multi_phi(i32 %a) {
; CHECK-LABEL: multi_phi:
entry:
  switch i32 %a, label %default [
    i32 0, label %case0
    i32 1, label %case1
    i32 2, label %case2
  ]

case0:
  br label %merge

case1:
  br label %merge

case2:
  br label %merge

default:
  br label %merge

merge:
  %r = phi i32 [ 10, %case0 ], [ 20, %case1 ], [ 30, %case2 ], [ 0, %default ]
  ret i32 %r
}

; Chained comparisons
define i32 @chained_cmp(i32 %a, i32 %b, i32 %c) {
; CHECK-LABEL: chained_cmp:
entry:
  %c1 = icmp sgt i32 %a, %b
  %c2 = icmp sgt i32 %b, %c
  %both = and i1 %c1, %c2
  %r = select i1 %both, i32 1, i32 0
  ret i32 %r
}

; i1 return (boolean)
define i1 @return_bool(i32 %a, i32 %b) {
; CHECK-LABEL: return_bool:
  %r = icmp eq i32 %a, %b
  ret i1 %r
}

; Multiple return values via struct
define { i32, i32 } @multi_ret(i32 %a, i32 %b) {
; CHECK-LABEL: multi_ret:
  %sum = add i32 %a, %b
  %diff = sub i32 %a, %b
  %r1 = insertvalue { i32, i32 } undef, i32 %sum, 0
  %r2 = insertvalue { i32, i32 } %r1, i32 %diff, 1
  ret { i32, i32 } %r2
}
