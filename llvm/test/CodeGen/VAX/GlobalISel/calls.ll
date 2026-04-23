; RUN: llc -mtriple=vax-unknown-netbsdelf -global-isel -global-isel-abort=1 < %s | FileCheck %s

; Exercise VAXCallLowering for the common call shapes: direct named
; callee, indirect (fptr) callee, void return, no-args, and a mix of
; argument widths that all fit on the stack per CC_VAX.

declare i32 @extern_add(i32, i32)
declare void @extern_void(i32)
declare i32 @extern_noargs()

define i32 @gisel_call_direct(i32 %a, i32 %b) {
; CHECK-LABEL: gisel_call_direct:
; CHECK: calls $2, extern_add
; CHECK: ret
  %r = call i32 @extern_add(i32 %a, i32 %b)
  ret i32 %r
}

define void @gisel_call_void(i32 %x) {
; CHECK-LABEL: gisel_call_void:
; CHECK: pushl
; CHECK: calls $1, extern_void
; CHECK: ret
  call void @extern_void(i32 %x)
  ret void
}

define i32 @gisel_call_noargs() {
; CHECK-LABEL: gisel_call_noargs:
; CHECK: calls $0, extern_noargs
; CHECK: ret
  %r = call i32 @extern_noargs()
  ret i32 %r
}

define i32 @gisel_call_indirect(ptr %fp, i32 %a) {
; CHECK-LABEL: gisel_call_indirect:
; CHECK: calls $1,
; CHECK: ret
  %r = call i32 %fp(i32 %a)
  ret i32 %r
}

; Small-constant arguments: CC_VAX widens i8/i16 args to i32 slots on
; the stack. Ensure we emit pushed widened values.
declare void @extern_small(i8, i16)

define void @gisel_call_small(i8 %b, i16 %w) {
; CHECK-LABEL: gisel_call_small:
; CHECK: calls $2, extern_small
; CHECK: ret
  call void @extern_small(i8 %b, i16 %w)
  ret void
}
