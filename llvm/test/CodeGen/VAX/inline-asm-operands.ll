; RUN: llc -mtriple=vax-unknown-netbsdelf < %s | FileCheck %s
;
; Inline asm with operand substitution ($0) and literal immediates ($$).

define i32 @asm_with_operand(i32 %a) {
; CHECK-LABEL: asm_with_operand:
; CHECK:       movl 4(%ap), %r0
; CHECK:       incl %r0
; CHECK:       ret
  %r = call i32 asm "incl $0", "=r,0"(i32 %a)
  ret i32 %r
}

define void @asm_with_immediate() {
; CHECK-LABEL: asm_with_immediate:
; CHECK:       movl $42, %r0
; CHECK:       ret
  call void asm sideeffect "movl $$42, %r0", ""()
  ret void
}
