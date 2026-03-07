; RUN: llc -march=vax < %s | FileCheck %s

; Test that i8 and i16 shift operations are correctly legalized.
; The VAX has no byte/word shift instructions, so these must promote to i32.

; --- i16 shifts ---

define i16 @shl_i16(i16 %a, i16 %b) {
; CHECK-LABEL: shl_i16:
; CHECK: ashl
; CHECK: ret
  %r = shl i16 %a, %b
  ret i16 %r
}

define i16 @srl_i16(i16 %a, i16 %b) {
; CHECK-LABEL: srl_i16:
; CHECK: extzv
; CHECK: ret
  %r = lshr i16 %a, %b
  ret i16 %r
}

define i16 @sra_i16(i16 %a, i16 %b) {
; CHECK-LABEL: sra_i16:
; CHECK: ashl
; CHECK: ret
  %r = ashr i16 %a, %b
  ret i16 %r
}

define i16 @shl_i16_const(i16 %a) {
; CHECK-LABEL: shl_i16_const:
; CHECK: ashl
; CHECK: ret
  %r = shl i16 %a, 3
  ret i16 %r
}

; --- i8 shifts ---

define i8 @shl_i8(i8 %a, i8 %b) {
; CHECK-LABEL: shl_i8:
; CHECK: ashl
; CHECK: ret
  %r = shl i8 %a, %b
  ret i8 %r
}

define i8 @srl_i8(i8 %a, i8 %b) {
; CHECK-LABEL: srl_i8:
; CHECK: extzv
; CHECK: ret
  %r = lshr i8 %a, %b
  ret i8 %r
}

define i8 @sra_i8(i8 %a, i8 %b) {
; CHECK-LABEL: sra_i8:
; CHECK: ashl
; CHECK: ret
  %r = ashr i8 %a, %b
  ret i8 %r
}

; --- i16 shift used in memory context (the original crash pattern) ---
; dwarf_form.c had: shl i16 1, %variable → used in AND → branch

define i16 @shl_i16_one_variable(i16 %bit) {
; CHECK-LABEL: shl_i16_one_variable:
; CHECK: ashl
; CHECK: ret
  %mask = shl i16 1, %bit
  %and = and i16 %mask, -30496
  %cmp = icmp ne i16 %and, 0
  %r = select i1 %cmp, i16 1, i16 0
  ret i16 %r
}
