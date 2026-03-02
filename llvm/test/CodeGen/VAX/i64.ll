; RUN: llc -march=vax < %s | FileCheck %s

; i64 add with carry propagation via ADWC
define i64 @test_add64(i64 %a, i64 %b) {
; CHECK-LABEL: test_add64:
; CHECK: addl3
; CHECK: adwc
  %r = add i64 %a, %b
  ret i64 %r
}

; i64 sub with borrow via SBWC
define i64 @test_sub64(i64 %a, i64 %b) {
; CHECK-LABEL: test_sub64:
; CHECK: subl3
; CHECK: sbwc
  %r = sub i64 %a, %b
  ret i64 %r
}

; i64 bitwise AND
define i64 @test_and64(i64 %a, i64 %b) {
; CHECK-LABEL: test_and64:
; CHECK: bicl3
; CHECK: bicl3
  %r = and i64 %a, %b
  ret i64 %r
}

; i64 bitwise OR
define i64 @test_or64(i64 %a, i64 %b) {
; CHECK-LABEL: test_or64:
; CHECK: bisl2
; CHECK: bisl2
  %r = or i64 %a, %b
  ret i64 %r
}

; i64 constant return
define i64 @test_ret_const() {
; CHECK-LABEL: test_ret_const:
; CHECK: movl $42, %r0
; CHECK: clrl %r1
  ret i64 42
}

; i64 left shift by constant
define i64 @test_shl64(i64 %a) {
; CHECK-LABEL: test_shl64:
; CHECK: ashl
  %r = shl i64 %a, 4
  ret i64 %r
}
