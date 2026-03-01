; RUN: llc -mtriple=vax-unknown-netbsdelf < %s | FileCheck %s

; Test inline assembly constraint 'r'

define i32 @inlineasm_r(i32 %a) {
; CHECK-LABEL: inlineasm_r:
; CHECK: movl 4(%ap), %r0
; CHECK: #APP
; CHECK: # nop
; CHECK: #NO_APP
  %r = call i32 asm "# nop", "=r,r"(i32 %a)
  ret i32 %r
}

; TODO: immediate constraint 'i' and operand substitution ($0) need
; printAsmOperand support — deferred to Phase 17E
