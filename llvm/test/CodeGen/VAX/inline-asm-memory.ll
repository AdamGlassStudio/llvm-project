; RUN: llc -mtriple=vax-unknown-netbsdelf < %s | FileCheck %s
;
; Inline asm with memory constraint ('m') on various address forms.
; Regression test: global address operands (PCRelWrapper) previously caused
; "Could not match memory address. Inline asm failure!" because
; SelectInlineAsmMemoryOperand rejected PCRelWrapper nodes.

@g = global i32 0, align 4

; Memory operand referencing a global variable.
define void @asm_mem_global() {
; CHECK-LABEL: asm_mem_global:
; CHECK: #APP
; CHECK: #NO_APP
; CHECK: ret
  call void asm sideeffect "# use $0", "*m,~{memory}"(ptr elementtype(i32) @g)
  ret void
}

; Memory operand referencing a pointer argument (register indirect).
define void @asm_mem_ptr(ptr %p) {
; CHECK-LABEL: asm_mem_ptr:
; CHECK: #APP
; CHECK: #NO_APP
; CHECK: ret
  call void asm sideeffect "# use $0", "*m,~{memory}"(ptr elementtype(i32) %p)
  ret void
}

; Memory operand with output (read-modify-write pattern).
; This is the pattern used by NetBSD's __cpu_simple_lock_try via BBSSI.
define i32 @asm_mem_output(ptr %p) {
; CHECK-LABEL: asm_mem_output:
; CHECK: #APP
; CHECK: #NO_APP
; CHECK: ret
  %r = call i32 asm sideeffect "clrl $0;incl $0", "=&r,*m,~{cc},~{memory}"(ptr elementtype(i32) %p)
  ret i32 %r
}

; Memory operand on a stack-allocated variable (FrameIndex).
define void @asm_mem_stack() {
; CHECK-LABEL: asm_mem_stack:
; CHECK: #APP
; CHECK: #NO_APP
; CHECK: ret
  %x = alloca i32, align 4
  store i32 0, ptr %x
  call void asm sideeffect "# use $0", "*m,~{memory}"(ptr elementtype(i32) %x)
  ret void
}
