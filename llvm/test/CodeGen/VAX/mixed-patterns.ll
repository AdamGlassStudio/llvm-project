; RUN: llc -mtriple=vax-unknown-netbsdelf < %s | FileCheck %s

; Test mixed-width and strength-reduction patterns

; Zero-extend i8 operands to i32 for add
define i32 @add_i8_promoted(i8 %a, i8 %b) {
; CHECK-LABEL: add_i8_promoted:
; CHECK: movzbl
; CHECK: movzbl
; CHECK: addl3
  %ea = zext i8 %a to i32
  %eb = zext i8 %b to i32
  %r = add i32 %ea, %eb
  ret i32 %r
}

; Sign-extend byte to longword
define i32 @sign_extend_byte(i8 %a) {
; CHECK-LABEL: sign_extend_byte:
; CHECK: cvtbl
  %r = sext i8 %a to i32
  ret i32 %r
}

; Multiply by power-of-2 should become shift
define i32 @mul_power_of_2(i32 %a) {
; CHECK-LABEL: mul_power_of_2:
; CHECK: ashl $3
  %r = mul i32 %a, 8
  ret i32 %r
}

; Add constant
define i32 @add_constant(i32 %a) {
; CHECK-LABEL: add_constant:
; CHECK: addl3 $42
  %r = add i32 %a, 42
  ret i32 %r
}

; Sub constant — lowered as addl3 with negative immediate
define i32 @sub_constant(i32 %a) {
; CHECK-LABEL: sub_constant:
; CHECK: addl3 $-42
  %r = sub i32 %a, 42
  ret i32 %r
}

; Identity function — arg in ap, ret in r0
define i32 @identity(i32 %a) {
; CHECK-LABEL: identity:
; CHECK: movl 4(%ap), %r0
; CHECK: ret
  ret i32 %a
}

; Void function
define void @nop() {
; CHECK-LABEL: nop:
; CHECK: ret
  ret void
}

; Three-operand add chain
define i32 @add3(i32 %a, i32 %b, i32 %c) {
; CHECK-LABEL: add3:
; CHECK: addl3
; CHECK: addl3
  %ab = add i32 %a, %b
  %r = add i32 %ab, %c
  ret i32 %r
}
