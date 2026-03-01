; RUN: llc -mtriple=vax-unknown-netbsdelf < %s | FileCheck %s

; Test integer comparison and setcc patterns

; Unsigned less-than
define i32 @ucmp_lt(i32 %a, i32 %b) {
; CHECK-LABEL: ucmp_lt:
; CHECK: cmpl
; CHECK: blssu
  %c = icmp ult i32 %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; Unsigned greater-than
define i32 @ucmp_gt(i32 %a, i32 %b) {
; CHECK-LABEL: ucmp_gt:
; CHECK: cmpl
; CHECK: bgtru
  %c = icmp ugt i32 %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; Signed equal
define i32 @scmp_eq(i32 %a, i32 %b) {
; CHECK-LABEL: scmp_eq:
; CHECK: cmpl
; CHECK: beql
  %c = icmp eq i32 %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; Signed not-equal
define i32 @scmp_ne(i32 %a, i32 %b) {
; CHECK-LABEL: scmp_ne:
; CHECK: cmpl
; CHECK: bneq
  %c = icmp ne i32 %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; Compare against zero — should use tstl
define i32 @cmp_zero(i32 %a) {
; CHECK-LABEL: cmp_zero:
; CHECK: tstl
; CHECK: beql
  %c = icmp eq i32 %a, 0
  %r = zext i1 %c to i32
  ret i32 %r
}

; Signed less-than
define i32 @scmp_slt(i32 %a, i32 %b) {
; CHECK-LABEL: scmp_slt:
; CHECK: cmpl
; CHECK: blss
  %c = icmp slt i32 %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; Signed greater-than
define i32 @scmp_sgt(i32 %a, i32 %b) {
; CHECK-LABEL: scmp_sgt:
; CHECK: cmpl
; CHECK: bgtr
  %c = icmp sgt i32 %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}
