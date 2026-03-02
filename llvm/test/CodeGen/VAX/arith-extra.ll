; RUN: llc -march=vax < %s | FileCheck %s

; Multiply
define i32 @test_mul(i32 %a, i32 %b) {
; CHECK-LABEL: test_mul:
; CHECK: mull2
  %r = mul i32 %a, %b
  ret i32 %r
}

; Signed divide
define i32 @test_sdiv(i32 %a, i32 %b) {
; CHECK-LABEL: test_sdiv:
; CHECK: divl3
  %r = sdiv i32 %a, %b
  ret i32 %r
}

; Negate
define i32 @test_neg(i32 %a) {
; CHECK-LABEL: test_neg:
; CHECK: mnegl
  %r = sub i32 0, %a
  ret i32 %r
}

; OR
define i32 @test_or(i32 %a, i32 %b) {
; CHECK-LABEL: test_or:
; CHECK: bisl2
  %r = or i32 %a, %b
  ret i32 %r
}

; XOR
define i32 @test_xor(i32 %a, i32 %b) {
; CHECK-LABEL: test_xor:
; CHECK: xorl2
  %r = xor i32 %a, %b
  ret i32 %r
}

; NOT (ones complement)
define i32 @test_not(i32 %a) {
; CHECK-LABEL: test_not:
; CHECK: mcoml
  %r = xor i32 %a, -1
  ret i32 %r
}

; Clear (return 0)
define i32 @test_zero() {
; CHECK-LABEL: test_zero:
; CHECK: clrl
  ret i32 0
}

; Callee-saved entry mask (value must survive call)
declare i32 @ext_func(i32)
define i32 @test_callee_saved(i32 %a, i32 %b) {
; CHECK-LABEL: test_callee_saved:
; CHECK: .word 64
  %x = call i32 @ext_func(i32 %a)
  %r = add i32 %x, %b
  ret i32 %r
}
