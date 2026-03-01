; RUN: llc -march=vax < %s | FileCheck %s

; Left shift by variable
define i32 @test_shl(i32 %a, i32 %b) {
; CHECK-LABEL: test_shl:
; CHECK: ashl
  %r = shl i32 %a, %b
  ret i32 %r
}

; Left shift by constant
define i32 @test_shl_const(i32 %a) {
; CHECK-LABEL: test_shl_const:
; CHECK: ashl $4
  %r = shl i32 %a, 4
  ret i32 %r
}

; Arithmetic right shift by variable
define i32 @test_ashr(i32 %a, i32 %b) {
; CHECK-LABEL: test_ashr:
; CHECK: mnegl
; CHECK: ashl
  %r = ashr i32 %a, %b
  ret i32 %r
}

; Arithmetic right shift by constant
define i32 @test_ashr_const(i32 %a) {
; CHECK-LABEL: test_ashr_const:
; CHECK: ashl $-4
  %r = ashr i32 %a, 4
  ret i32 %r
}

; Logical right shift by constant
define i32 @test_lshr_const(i32 %a) {
; CHECK-LABEL: test_lshr_const:
; CHECK: rotl $28
; CHECK: bicl3
  %r = lshr i32 %a, 4
  ret i32 %r
}

; Logical right shift by variable
define i32 @test_lshr(i32 %a, i32 %b) {
; CHECK-LABEL: test_lshr:
; CHECK: mnegl
; CHECK: ashl
; CHECK: bicl3
  %r = lshr i32 %a, %b
  ret i32 %r
}

; Rotate left
define i32 @test_rotl(i32 %a, i32 %b) {
; CHECK-LABEL: test_rotl:
; CHECK: rotl
  %shl = shl i32 %a, %b
  %sub = sub i32 32, %b
  %shr = lshr i32 %a, %sub
  %r = or i32 %shl, %shr
  ret i32 %r
}
