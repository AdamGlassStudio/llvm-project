; RUN: llc -mtriple=vax-unknown-netbsdelf < %s | FileCheck %s
; XFAIL: *
;
; Bug: inline asm operand substitution ($0) conflicts with VAX GAS
; immediate prefix ($). printAsmOperand not wired up yet.
; Fix: implement printAsmOperand in VAXAsmPrinter to use a different
; substitution syntax or properly escape operand references.

define i32 @asm_with_operand(i32 %a) {
; CHECK-LABEL: asm_with_operand:
; CHECK: incl
  %r = call i32 asm "incl $0", "=r,0"(i32 %a)
  ret i32 %r
}

define void @asm_with_immediate() {
; CHECK-LABEL: asm_with_immediate:
; CHECK: movl
  call void asm sideeffect "movl $$42, %r0", ""()
  ret void
}
