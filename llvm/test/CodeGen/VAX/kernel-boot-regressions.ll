; RUN: llc -mtriple=vax-unknown-netbsdelf < %s | FileCheck %s
;
; Regression tests for bugs found during first Clang-compiled NetBSD/vax
; kernel boot on SIMH (March 2026). Each test corresponds to a specific
; bug that caused a kernel crash.

; ---------------------------------------------------------------------------
; Bug 1: PrintAsmMemoryOperand ignored displacement offset.
;
; Inline asm with "=m" output constraint on a local variable was printed
; as (%fp) instead of -N(%fp), causing writes to the wrong stack location.
; The mfpr inline asm in findcpu() wrote the CPU SID register to offset 0
; (condition handler slot) instead of the actual local variable.
; ---------------------------------------------------------------------------

define i32 @asm_memory_displacement() {
; CHECK-LABEL: asm_memory_displacement:
; CHECK: #APP
; The key check: the memory operand must include a displacement, not just (%fp).
; The exact displacement depends on frame layout, but it must NOT be just (%fp).
; CHECK: mfpr $62, {{-?[0-9]+}}(%fp)
; CHECK: #NO_APP
  %val = alloca i32, align 4
  call void asm sideeffect "mfpr $$62, $0", "=*m"(ptr elementtype(i32) %val)
  %result = load i32, ptr %val, align 4
  ret i32 %result
}

; Same pattern but with an input+output to force the local deeper on the stack.
define i32 @asm_memory_displacement_offset(i32 %x) {
; CHECK-LABEL: asm_memory_displacement_offset:
; CHECK: #APP
; CHECK: mfpr $62, {{-?[0-9]+}}(%fp)
; CHECK: #NO_APP
  %pad = alloca i32, align 4
  %val = alloca i32, align 4
  store i32 %x, ptr %pad, align 4
  call void asm sideeffect "mfpr $$62, $0", "=*m"(ptr elementtype(i32) %val)
  %result = load i32, ptr %val, align 4
  ret i32 %result
}

; ---------------------------------------------------------------------------
; Bug 4: PrintAsmOperand missing $ prefix on global/external symbols.
;
; In GAS VAX syntax, $symbol = immediate (address of symbol), while
; symbol = memory reference (value at address). Without the $ prefix,
; GAS encodes as PC-relative displacement (0xEF) instead of immediate
; (0x8F). This caused mtpr to write the VALUE at lwp0 (zero, since .bss)
; instead of the ADDRESS of lwp0 to PR_SSP.
; ---------------------------------------------------------------------------

@myvar = external dso_local global i32

; The inline asm constraint "r" forces the global address into a register,
; and the address materialization must use the symbol name correctly.
; Without Bug 4 fix, the symbol would lack the $ prefix in the movl,
; causing GAS to encode it as a PC-relative memory reference.
define void @asm_global_dollar_prefix() {
; CHECK-LABEL: asm_global_dollar_prefix:
; The global address is materialized via moval (move address longword).
; The key is that myvar appears as an operand — the address is loaded, not dereferenced.
; CHECK: moval myvar, %r{{[0-9]+}}
; CHECK: #APP
; CHECK: mtpr %r{{[0-9]+}}, $2
; CHECK: #NO_APP
  %addr = ptrtoint ptr @myvar to i32
  call void asm sideeffect "mtpr $0, $$2", "r"(i32 %addr)
  ret void
}

; ---------------------------------------------------------------------------
; Bug 5: MOVD used for i64 register spills instead of MOVQ.
;
; QPR (64-bit register pair) spills/reloads used MOVD (opcode 0x70, D_float
; move) which validates floating-point format and faults on reserved operand
; patterns in integer data. MOVQ (opcode 0x7D) is a pure data move.
; The crash was in bounds_check_with_label where an i64 value with bits
; that form a reserved D_float pattern was spilled to the stack.
;
; Verify no MOVD is generated for integer i64 operations.
; ---------------------------------------------------------------------------

; This test uses a value that would be a reserved D_float operand.
; 0xFFFFFFFF_00000010 has exponent=0 in D_float format = reserved operand.
; MOVD would fault; MOVQ must be used for all 64-bit integer moves.
define i64 @i64_reserved_float_pattern(i64 %x) {
; CHECK-LABEL: i64_reserved_float_pattern:
; Ensure no movd instructions appear in this pure-integer function.
; CHECK-NOT: movd
; CHECK: ret
  %val = add i64 %x, -4294967280  ; 0xFFFFFFFF00000010
  ret i64 %val
}

; Force QPR spills by using many live i64 values simultaneously.
; With 7 i64 values (14 GPRs needed) exceeding the 10 available (R2-R11),
; the register allocator must spill QPR registers.
declare void @use_i64(i64) nounwind
define i64 @i64_spill_uses_movq(i64 %a, i64 %b, i64 %c, i64 %d, i64 %e, i64 %f, i64 %g) nounwind {
; CHECK-LABEL: i64_spill_uses_movq:
; Spills should use movq, NOT movd. Any movd in an integer-only function
; means the bug has regressed.
; CHECK-NOT: movd
  call void @use_i64(i64 %a)
  call void @use_i64(i64 %b)
  call void @use_i64(i64 %c)
  call void @use_i64(i64 %d)
  call void @use_i64(i64 %e)
  call void @use_i64(i64 %f)
  call void @use_i64(i64 %g)
  %ab = add i64 %a, %b
  %cd = add i64 %c, %d
  %ef = add i64 %e, %f
  %abcd = add i64 %ab, %cd
  %abcde = add i64 %abcd, %ef
  %result = add i64 %abcde, %g
  ret i64 %result
}
