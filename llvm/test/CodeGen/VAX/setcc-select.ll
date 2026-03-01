; RUN: llc -mtriple=vax-unknown-netbsdelf < %s | FileCheck %s

; Test unsigned comparisons and setcc patterns

define i32 @setcc_eq(i32 %a, i32 %b) {
; CHECK-LABEL: setcc_eq:
; CHECK: cmpl
  %c = icmp eq i32 %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

define i32 @setcc_ne(i32 %a, i32 %b) {
; CHECK-LABEL: setcc_ne:
; CHECK: cmpl
  %c = icmp ne i32 %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

define i32 @setcc_ult(i32 %a, i32 %b) {
; CHECK-LABEL: setcc_ult:
; CHECK: cmpl
  %c = icmp ult i32 %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

define i32 @setcc_uge(i32 %a, i32 %b) {
; CHECK-LABEL: setcc_uge:
; CHECK: cmpl
  %c = icmp uge i32 %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

define i32 @setcc_sgt(i32 %a, i32 %b) {
; CHECK-LABEL: setcc_sgt:
; CHECK: cmpl
  %c = icmp sgt i32 %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

define i32 @setcc_sle(i32 %a, i32 %b) {
; CHECK-LABEL: setcc_sle:
; CHECK: cmpl
  %c = icmp sle i32 %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; Compare against zero should use tstl
define i32 @cmp_zero(i32 %a) {
; CHECK-LABEL: cmp_zero:
; CHECK: tstl
  %c = icmp eq i32 %a, 0
  %r = zext i1 %c to i32
  ret i32 %r
}

; Select pattern (conditional move)
define i32 @select_i32(i32 %a, i32 %b, i32 %c) {
; CHECK-LABEL: select_i32:
; CHECK: tstl
  %cond = icmp eq i32 %a, 0
  %r = select i1 %cond, i32 %b, i32 %c
  ret i32 %r
}
