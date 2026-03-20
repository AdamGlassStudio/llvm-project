; RUN: llc -mtriple=vax-unknown-netbsdelf < %s | FileCheck %s

; Test DWARF CFI frame info for the CALLS frame and callee-saved registers.
; VAX CALLS frame layout (from high to low address):
;   FP+16: saved PC
;   FP+12: saved FP
;   FP+8:  saved AP
;   FP+4:  PSW + entry mask + SPA + stack alignment
;   FP+0:  condition handler (0)
;   FP-4:  highest-numbered saved register (from entry mask)
;   FP-8:  next saved register
;   ...
;   below saved regs: locals

declare void @use(ptr)
declare i32 @bar(i32)

; CHECK-LABEL: basic_frame:
; CHECK: .cfi_startproc
; CHECK: .cfi_def_cfa %fp, 0
; CHECK: .cfi_offset %pc, 16
; CHECK: .cfi_offset %fp, 12
; CHECK: .cfi_offset %ap, 8
; CHECK-NOT: .cfi_offset %r
define void @basic_frame() {
  %a = alloca [64 x i8], align 4
  call void @use(ptr %a)
  ret void
}

; Test that callee-saved registers get CFI offset directives.
; The function uses enough callee-saved regs across calls to force saves.
; Entry mask saves regs in descending order: highest-numbered at FP-4.

; CHECK-LABEL: callee_saved_regs:
; CHECK: .cfi_startproc
; CHECK: .cfi_def_cfa %fp, 0
; CHECK: .cfi_offset %pc, 16
; CHECK: .cfi_offset %fp, 12
; CHECK: .cfi_offset %ap, 8
; CHECK: .cfi_offset %r{{[0-9]+}}, -4
define i32 @callee_saved_regs(i32 %a, i32 %b) {
  %r1 = call i32 @bar(i32 %a)
  %r2 = call i32 @bar(i32 %b)
  %sum = add i32 %r1, %r2
  ret i32 %sum
}

; Test with multiple callee-saved registers.
; CHECK-LABEL: multi_callee_saved:
; CHECK: .cfi_startproc
; CHECK: .cfi_def_cfa %fp, 0
; CHECK: .cfi_offset %pc, 16
; CHECK: .cfi_offset %fp, 12
; CHECK: .cfi_offset %ap, 8
; CHECK-DAG: .cfi_offset %r7, -4
; CHECK-DAG: .cfi_offset %r6, -8
define i32 @multi_callee_saved(i32 %a, i32 %b, i32 %c, i32 %d) {
  %r1 = call i32 @bar(i32 %a)
  %r2 = call i32 @bar(i32 %b)
  %r3 = call i32 @bar(i32 %c)
  %r4 = call i32 @bar(i32 %d)
  %s1 = add i32 %r1, %r2
  %s2 = add i32 %s1, %r3
  %s3 = add i32 %s2, %r4
  ret i32 %s3
}
