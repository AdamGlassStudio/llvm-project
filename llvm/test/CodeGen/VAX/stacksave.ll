; RUN: llc -mtriple=vax-unknown-netbsdelf < %s | FileCheck %s
;
; Stack save/restore for VLA scoping. SP is saved and restored
; around dynamic allocations.

declare ptr @llvm.stacksave()
declare void @llvm.stackrestore(ptr)

define i32 @stacksave_test(i32 %n) {
; CHECK-LABEL: stacksave_test:
; CHECK:       movl %sp, %r0
; CHECK:       movl 4(%ap), %r1
; CHECK:       ashl $2, %r1, %r1
; CHECK:       subl3 %r1, %sp, %r1
; CHECK:       movl %r1, %sp
; CHECK:       movl $99, (%r1)
; CHECK:       movl %r0, %sp
; CHECK:       movl $99, %r0
; CHECK:       ret
  %sp = call ptr @llvm.stacksave()
  %p = alloca i32, i32 %n
  store i32 99, ptr %p
  %v = load i32, ptr %p
  call void @llvm.stackrestore(ptr %sp)
  ret i32 %v
}
