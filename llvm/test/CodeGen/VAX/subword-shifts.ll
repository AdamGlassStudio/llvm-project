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

; --- Two-byte combine pattern (caused infinite loop in distribution build) ---
; Pattern: ((unsigned short)p[0] << 8) | p[1]
; This was the root cause of 6 libc files hanging during NetBSD build.

define i16 @twobyte_combine(ptr %p) {
; CHECK-LABEL: twobyte_combine:
; CHECK: ashl	$8
; CHECK: bisw3
; CHECK: ret
  %b0 = load i8, ptr %p, align 1
  %conv = zext i8 %b0 to i16
  %shl = shl nuw i16 %conv, 8
  %arrayidx2 = getelementptr inbounds i8, ptr %p, i32 1
  %b1 = load i8, ptr %arrayidx2, align 1
  %conv3 = zext i8 %b1 to i16
  %or = or disjoint i16 %shl, %conv3
  ret i16 %or
}

; --- i8 shift-and-mask (common in bitfield code) ---

define i8 @shl_i8_const_mask(i8 %a) {
; CHECK-LABEL: shl_i8_const_mask:
; CHECK: ashl
; CHECK: ret
  %shl = shl i8 %a, 4
  %mask = and i8 %shl, -16
  ret i8 %mask
}

; --- i16 logical right shift by constant (extzv path) ---

define i16 @srl_i16_const(i16 %a) {
; CHECK-LABEL: srl_i16_const:
; CHECK: extzv
; CHECK: ret
  %r = lshr i16 %a, 5
  ret i16 %r
}

; --- i8 arithmetic right shift by constant ---

define i8 @sra_i8_const(i8 %a) {
; CHECK-LABEL: sra_i8_const:
; CHECK: ashl
; CHECK: ret
  %r = ashr i8 %a, 3
  ret i8 %r
}
