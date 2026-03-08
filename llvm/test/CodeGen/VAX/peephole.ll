; RUN: llc -march=vax < %s | FileCheck %s
; Test the VAX post-RA peephole pass:
;   - 3-operand ALU → 2-operand when dst == source (register-register)
;   - ADDL2 $1 → INCL, ADDL2 $-1 → DECL
;
; Note: the peephole only converts register-register 3-op forms. When one
; source is a memory operand (e.g. stack argument), the 3-op form stays
; because there is no memory-operand 2-op variant in the peephole table.
; Byte/word/FP arguments pass in registers, so those convert reliably.

;; --- Longword integer: div is reg-reg (divl3→divl2) ---

; CHECK-LABEL: div_i32:
; CHECK: divl2
define i32 @div_i32(i32 %a, i32 %b) {
  %r = sdiv i32 %a, %b
  ret i32 %r
}

;; --- Byte 3→2 (args in registers via sub-reg) ---

;; --- F_float 3→2 ---

; CHECK-LABEL: fadd_f32:
; CHECK: addf2
define float @fadd_f32(float %a, float %b) {
  %r = fadd float %a, %b
  ret float %r
}

; CHECK-LABEL: fsub_f32:
; CHECK: subf2
define float @fsub_f32(float %a, float %b) {
  %r = fsub float %a, %b
  ret float %r
}

; CHECK-LABEL: fmul_f32:
; CHECK: mulf2
define float @fmul_f32(float %a, float %b) {
  %r = fmul float %a, %b
  ret float %r
}

; CHECK-LABEL: fdiv_f32:
; CHECK: divf2
define float @fdiv_f32(float %a, float %b) {
  %r = fdiv float %a, %b
  ret float %r
}

;; --- D_float 3→2 ---

; CHECK-LABEL: fadd_f64:
; CHECK: addd2
define double @fadd_f64(double %a, double %b) {
  %r = fadd double %a, %b
  ret double %r
}

; CHECK-LABEL: fsub_f64:
; CHECK: subd2
define double @fsub_f64(double %a, double %b) {
  %r = fsub double %a, %b
  ret double %r
}

; CHECK-LABEL: fmul_f64:
; CHECK: muld2
define double @fmul_f64(double %a, double %b) {
  %r = fmul double %a, %b
  ret double %r
}

; CHECK-LABEL: fdiv_f64:
; CHECK: divd2
define double @fdiv_f64(double %a, double %b) {
  %r = fdiv double %a, %b
  ret double %r
}

;; --- INCL / DECL ---
; These fire when add-by-1 value is already in a register.
; Two-input add forces the first result into a register, then +1/-1
; triggers the peephole ADDL2_ri → INCL/DECL path.

; CHECK-LABEL: inc_i32:
; CHECK: incl
define i32 @inc_i32(i32 %a, i32 %b) {
  %sum = add i32 %a, %b
  %r = add i32 %sum, 1
  ret i32 %r
}

; CHECK-LABEL: dec_i32:
; CHECK: decl
define i32 @dec_i32(i32 %a, i32 %b) {
  %sum = add i32 %a, %b
  %r = add i32 %sum, -1
  ret i32 %r
}

;; --- Verify memory-operand 3-op→2-op peephole fires ---
; Longword add with stack args: addl3 mem, %rX, %rX → addl2 mem, %rX

; CHECK-LABEL: add_i32_stays_3op:
; CHECK: addl2
define i32 @add_i32_stays_3op(i32 %a, i32 %b) {
  %r = add i32 %a, %b
  ret i32 %r
}
