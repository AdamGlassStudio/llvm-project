; RUN: llc -mtriple=vax-unknown-netbsdelf -global-isel -global-isel-abort=1 < %s | FileCheck %s

@G32 = external global i32
@G8  = external global i8
@G16 = external global i16

define i32 @gisel_load_i32() {
; CHECK-LABEL: gisel_load_i32:
; CHECK: movl
; CHECK: ret
  %r = load i32, ptr @G32
  ret i32 %r
}

define void @gisel_store_i32(i32 %v) {
; CHECK-LABEL: gisel_store_i32:
; CHECK: movl
; CHECK: ret
  store i32 %v, ptr @G32
  ret void
}

define i32 @gisel_load_zext_i8() {
; CHECK-LABEL: gisel_load_zext_i8:
; CHECK: movzbl
; CHECK: ret
  %b = load i8, ptr @G8
  %r = zext i8 %b to i32
  ret i32 %r
}

define i32 @gisel_load_sext_i16() {
; CHECK-LABEL: gisel_load_sext_i16:
; CHECK: cvtwl
; CHECK: ret
  %w = load i16, ptr @G16
  %r = sext i16 %w to i32
  ret i32 %r
}

define ptr @gisel_ptr_add(ptr %p, i32 %i) {
; CHECK-LABEL: gisel_ptr_add:
; CHECK: addl
; CHECK: ret
  %r = getelementptr i32, ptr %p, i32 %i
  ret ptr %r
}

define i32 @gisel_icmp_eq(i32 %a, i32 %b) {
; CHECK-LABEL: gisel_icmp_eq:
; CHECK: cmpl
; CHECK: ret
  %c = icmp eq i32 %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

define i32 @gisel_select(i32 %c, i32 %a, i32 %b) {
; CHECK-LABEL: gisel_select:
; CHECK: ret
  %p = icmp ne i32 %c, 0
  %r = select i1 %p, i32 %a, i32 %b
  ret i32 %r
}

define i32 @gisel_branch(i32 %a, i32 %b) {
; CHECK-LABEL: gisel_branch:
; CHECK: cmpl
; CHECK: ret
entry:
  %c = icmp slt i32 %a, %b
  br i1 %c, label %yes, label %no
yes:
  ret i32 1
no:
  ret i32 0
}
