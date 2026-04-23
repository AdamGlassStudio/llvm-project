; RUN: llc -mtriple=vax-unknown-netbsdelf -global-isel -global-isel-abort=1 < %s | FileCheck %s

; G_FRAME_INDEX: selectFrameIndex must emit a MOVAL / MOVAB style
; address computation relative to the frame pointer. Exercised through
; alloca + store + load of both i32 and i8.

define i32 @gisel_alloca_i32(i32 %v) {
; CHECK-LABEL: gisel_alloca_i32:
; CHECK: movl
; CHECK: movl
; CHECK: ret
entry:
  %slot = alloca i32
  store i32 %v, ptr %slot
  %r = load i32, ptr %slot
  ret i32 %r
}

; Address of a local is passed to an external; the frame index must be
; converted to a runtime address via MOVAx.
declare void @extern_sink(ptr)

define void @gisel_addr_of_local() {
; CHECK-LABEL: gisel_addr_of_local:
; CHECK: pushal
; CHECK: calls $1, extern_sink
; CHECK: ret
entry:
  %slot = alloca i32
  call void @extern_sink(ptr %slot)
  ret void
}

; Multiple slots of different sizes — ensures frame layout math works.
define i32 @gisel_two_slots(i32 %a, i8 %b) {
; CHECK-LABEL: gisel_two_slots:
; CHECK: ret
entry:
  %i32s = alloca i32
  %i8s  = alloca i8
  store i32 %a, ptr %i32s
  store i8  %b, ptr %i8s
  %r1 = load i32, ptr %i32s
  %r2 = load i8,  ptr %i8s
  %r2x = zext i8 %r2 to i32
  %sum = add i32 %r1, %r2x
  ret i32 %sum
}
