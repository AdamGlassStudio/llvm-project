; RUN: llc -mtriple=vax-unknown-netbsdelf < %s | FileCheck %s
;
; Dynamic stack allocation (C VLAs). Expands to SP adjustment.

define i32 @dyn_alloca(i32 %n) {
; CHECK-LABEL: dyn_alloca:
; CHECK:       movl 4(%ap), %r0
; CHECK:       ashl $2, %r0, %r0
; CHECK:       subl3 %r0, %sp, %r0
; CHECK:       movl %r0, %sp
; CHECK:       movl $42, (%r0)
; CHECK:       movl $42, %r0
; CHECK:       ret
  %p = alloca i32, i32 %n
  store i32 42, ptr %p
  %v = load i32, ptr %p
  ret i32 %v
}
