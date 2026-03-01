; RUN: llc -mtriple=vax-unknown-netbsdelf < %s | FileCheck %s

; Test integer type promotion and narrowing operations

; i8 arithmetic (promoted to i32 on VAX)
define i8 @add_i8(i8 %a, i8 %b) {
; CHECK-LABEL: add_i8:
  %r = add i8 %a, %b
  ret i8 %r
}

define i8 @sub_i8(i8 %a, i8 %b) {
; CHECK-LABEL: sub_i8:
  %r = sub i8 %a, %b
  ret i8 %r
}

; i16 arithmetic
define i16 @add_i16(i16 %a, i16 %b) {
; CHECK-LABEL: add_i16:
  %r = add i16 %a, %b
  ret i16 %r
}

; Mixed width: i8 extended to i32 then used
define i32 @zext_add(i8 %a, i8 %b) {
; CHECK-LABEL: zext_add:
; CHECK: movzbl
  %ea = zext i8 %a to i32
  %eb = zext i8 %b to i32
  %r = add i32 %ea, %eb
  ret i32 %r
}

define i32 @sext_add(i8 %a, i8 %b) {
; CHECK-LABEL: sext_add:
; CHECK: cvtbl
  %ea = sext i8 %a to i32
  %eb = sext i8 %b to i32
  %r = add i32 %ea, %eb
  ret i32 %r
}

; Truncate and store
define void @trunc_store_i8(i32 %v, ptr %p) {
; CHECK-LABEL: trunc_store_i8:
; CHECK: movb
  %t = trunc i32 %v to i8
  store i8 %t, ptr %p
  ret void
}

define void @trunc_store_i16(i32 %v, ptr %p) {
; CHECK-LABEL: trunc_store_i16:
; CHECK: movw
  %t = trunc i32 %v to i16
  store i16 %t, ptr %p
  ret void
}

; i8 comparison
define i1 @cmp_i8(i8 %a, i8 %b) {
; CHECK-LABEL: cmp_i8:
  %r = icmp eq i8 %a, %b
  ret i1 %r
}

; i16 comparison
define i1 @cmp_i16(i16 %a, i16 %b) {
; CHECK-LABEL: cmp_i16:
  %r = icmp sgt i16 %a, %b
  ret i1 %r
}
