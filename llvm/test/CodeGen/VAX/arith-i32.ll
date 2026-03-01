; RUN: llc -mtriple=vax-unknown-netbsdelf < %s | FileCheck %s

@x = global i32 10
@y = global i32 3

; CHECK-LABEL: test_add_rr:
; CHECK: addl3
; CHECK: ret
define i32 @test_add_rr() {
  %a = load i32, ptr @x
  %b = load i32, ptr @y
  %r = add i32 %a, %b
  ret i32 %r
}

; CHECK-LABEL: test_add_ri:
; CHECK: addl3 $42
; CHECK: ret
define i32 @test_add_ri() {
  %a = load i32, ptr @x
  %r = add i32 %a, 42
  ret i32 %r
}

; CHECK-LABEL: test_sub_rr:
; CHECK: subl3
; CHECK: ret
define i32 @test_sub_rr() {
  %a = load i32, ptr @x
  %b = load i32, ptr @y
  %r = sub i32 %a, %b
  ret i32 %r
}

; CHECK-LABEL: test_mul_rr:
; CHECK: mull3
; CHECK: ret
define i32 @test_mul_rr() {
  %a = load i32, ptr @x
  %b = load i32, ptr @y
  %r = mul i32 %a, %b
  ret i32 %r
}

; CHECK-LABEL: test_sdiv_rr:
; CHECK: divl3
; CHECK: ret
define i32 @test_sdiv_rr() {
  %a = load i32, ptr @x
  %b = load i32, ptr @y
  %r = sdiv i32 %a, %b
  ret i32 %r
}

; CHECK-LABEL: test_or_rr:
; CHECK: bisl3
; CHECK: ret
define i32 @test_or_rr() {
  %a = load i32, ptr @x
  %b = load i32, ptr @y
  %r = or i32 %a, %b
  ret i32 %r
}

; CHECK-LABEL: test_xor_rr:
; CHECK: xorl3
; CHECK: ret
define i32 @test_xor_rr() {
  %a = load i32, ptr @x
  %b = load i32, ptr @y
  %r = xor i32 %a, %b
  ret i32 %r
}

; AND(A, B) is lowered to BICL3(MCOML(B), A) — two instructions.
; CHECK-LABEL: test_and_rr:
; CHECK: mcoml
; CHECK: bicl3
; CHECK: ret
define i32 @test_and_rr() {
  %a = load i32, ptr @x
  %b = load i32, ptr @y
  %r = and i32 %a, %b
  ret i32 %r
}

; AND with constant: LLVM combines (load i32) & 0xFF → movzbl (single insn).
; CHECK-LABEL: test_and_imm:
; CHECK: movzbl
; CHECK: ret
define i32 @test_and_imm() {
  %a = load i32, ptr @x
  %r = and i32 %a, 255
  ret i32 %r
}

; CHECK-LABEL: test_not:
; CHECK: mcoml
; CHECK: ret
define i32 @test_not() {
  %a = load i32, ptr @x
  %r = xor i32 %a, -1
  ret i32 %r
}

; CHECK-LABEL: test_neg:
; CHECK: mnegl
; CHECK: ret
define i32 @test_neg() {
  %a = load i32, ptr @x
  %r = sub i32 0, %a
  ret i32 %r
}
