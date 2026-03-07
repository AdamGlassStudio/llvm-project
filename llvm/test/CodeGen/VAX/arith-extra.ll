; RUN: llc -march=vax < %s | FileCheck %s

; Multiply
define i32 @test_mul(i32 %a, i32 %b) {
; CHECK-LABEL: test_mul:
; CHECK: movl	8(%ap), %r0
; CHECK-NEXT: mull3	4(%ap), %r0, %r0
; CHECK-NEXT: ret
; CHECK-NEXT: .Lfunc_end0:
  %r = mul i32 %a, %b
  ret i32 %r
}

; Signed divide
define i32 @test_sdiv(i32 %a, i32 %b) {
; CHECK-LABEL: test_sdiv:
; CHECK: movl	4(%ap), %r0
; CHECK-NEXT: movl	8(%ap), %r1
; CHECK-NEXT: divl3	%r1, %r0, %r0
; CHECK-NEXT: ret
; CHECK-NEXT: .Lfunc_end1:
  %r = sdiv i32 %a, %b
  ret i32 %r
}

; Negate
define i32 @test_neg(i32 %a) {
; CHECK-LABEL: test_neg:
; CHECK: clrl	%r0
; CHECK-NEXT: subl3	4(%ap), %r0, %r0
; CHECK-NEXT: ret
; CHECK-NEXT: .Lfunc_end2:
  %r = sub i32 0, %a
  ret i32 %r
}

; OR
define i32 @test_or(i32 %a, i32 %b) {
; CHECK-LABEL: test_or:
; CHECK: movl	8(%ap), %r0
; CHECK-NEXT: bisl3	4(%ap), %r0, %r0
; CHECK-NEXT: ret
; CHECK-NEXT: .Lfunc_end3:
  %r = or i32 %a, %b
  ret i32 %r
}

; XOR
define i32 @test_xor(i32 %a, i32 %b) {
; CHECK-LABEL: test_xor:
; CHECK: movl	8(%ap), %r0
; CHECK-NEXT: xorl3	4(%ap), %r0, %r0
; CHECK-NEXT: ret
; CHECK-NEXT: .Lfunc_end4:
  %r = xor i32 %a, %b
  ret i32 %r
}

; NOT (ones complement)
define i32 @test_not(i32 %a) {
; CHECK-LABEL: test_not:
; CHECK: movl	$-1, %r0
; CHECK-NEXT: xorl3	4(%ap), %r0, %r0
; CHECK-NEXT: ret
; CHECK-NEXT: .Lfunc_end5:
  %r = xor i32 %a, -1
  ret i32 %r
}

; Clear (return 0)
define i32 @test_zero() {
; CHECK-LABEL: test_zero:
; CHECK: clrl	%r0
; CHECK-NEXT: ret
; CHECK-NEXT: .Lfunc_end6:
  ret i32 0
}

; Callee-saved entry mask (value must survive call)
declare i32 @ext_func(i32)
define i32 @test_callee_saved(i32 %a, i32 %b) {
; CHECK-LABEL: test_callee_saved:
; CHECK: movl	4(%ap), %r0
; CHECK-NEXT: pushl	%r0
; CHECK-NEXT: calls	$1, ext_func
; CHECK-NEXT: addl3	8(%ap), %r0, %r0
; CHECK-NEXT: ret
; CHECK-NEXT: .Lfunc_end7:
  %x = call i32 @ext_func(i32 %a)
  %r = add i32 %x, %b
  ret i32 %r
}
