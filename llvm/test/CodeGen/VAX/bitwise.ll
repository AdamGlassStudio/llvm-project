; RUN: llc -mtriple=vax-unknown-netbsdelf < %s | FileCheck %s

; Test bitwise operations with immediates

; AND with 255 optimizes to zero-extend byte
define i32 @and_imm(i32 %a) {
; CHECK-LABEL: and_imm:
; CHECK: movzbl
  %r = and i32 %a, 255
  ret i32 %r
}

define i32 @or_imm(i32 %a) {
; CHECK-LABEL: or_imm:
; CHECK: bisl
  %r = or i32 %a, 255
  ret i32 %r
}

define i32 @xor_imm(i32 %a) {
; CHECK-LABEL: xor_imm:
; CHECK: xorl
  %r = xor i32 %a, 255
  ret i32 %r
}

; Bit complement
define i32 @not_i32(i32 %a) {
; CHECK-LABEL: not_i32:
; CHECK: mcoml
  %r = xor i32 %a, -1
  ret i32 %r
}

; Shift left by constant
define i32 @shl_const(i32 %a) {
; CHECK-LABEL: shl_const:
; CHECK: ashl $4
  %r = shl i32 %a, 4
  ret i32 %r
}

; Arithmetic shift right by constant
define i32 @ashr_const(i32 %a) {
; CHECK-LABEL: ashr_const:
; CHECK: ashl $-4
  %r = ashr i32 %a, 4
  ret i32 %r
}

; Logical shift right by constant (uses rotl + mask)
define i32 @lshr_const(i32 %a) {
; CHECK-LABEL: lshr_const:
; CHECK: rotl $28
; CHECK: bicl3
  %r = lshr i32 %a, 4
  ret i32 %r
}

; Byte-level AND (mask low byte) - optimized to movzbl
define i32 @mask_byte(i32 %a) {
; CHECK-LABEL: mask_byte:
; CHECK: movzbl
  %r = and i32 %a, 255
  ret i32 %r
}

; Byte-level AND (mask low 16 bits) - optimized to movzwl
define i32 @mask_halfword(i32 %a) {
; CHECK-LABEL: mask_halfword:
; CHECK: movzwl
  %r = and i32 %a, 65535
  ret i32 %r
}
