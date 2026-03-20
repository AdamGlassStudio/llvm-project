; RUN: llc -mtriple=vax-unknown-netbsdelf < %s | FileCheck %s

; Test DWARF CFI frame info for callee-saved registers.
; VAX CALLS frame layout (from high to low address):
;   FP+16: saved PC
;   FP+12: saved FP
;   FP+8:  saved AP
;   FP+4:  PSW + entry mask + SPA + stack alignment
;   FP+0:  condition handler (0)
;   below FP: callee-saved registers, then locals

declare void @use(ptr)

; CHECK-LABEL: callee_saved_regs:
; CHECK: .cfi_startproc
; CHECK: .cfi_def_cfa %fp, 0
; CHECK: .cfi_offset %pc, 16
; CHECK: .cfi_offset %fp, 12
; CHECK: .cfi_offset %ap, 8
define void @callee_saved_regs() {
  %a = alloca [64 x i8], align 4
  call void @use(ptr %a)
  ret void
}
