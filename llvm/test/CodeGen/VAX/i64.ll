; RUN: llc -march=vax < %s | FileCheck %s

; i64 add with carry propagation via ADWC
define i64 @test_add64(i64 %a, i64 %b) {
; CHECK-LABEL: test_add64:
; CHECK: movl	12(%ap), %r0
; CHECK-NEXT: movl	4(%ap), %r2
; CHECK-NEXT: movl	16(%ap), %r1
; CHECK-NEXT: movl	8(%ap), %r3
; CHECK-NEXT: addl3	%r2, %r0, %r0
; CHECK-NEXT: adwc	%r3, %r1
; CHECK-NEXT: ret
; CHECK-NEXT: .Lfunc_end0:
  %r = add i64 %a, %b
  ret i64 %r
}

; i64 sub with borrow via SBWC
define i64 @test_sub64(i64 %a, i64 %b) {
; CHECK-LABEL: test_sub64:
; CHECK: movl	4(%ap), %r0
; CHECK-NEXT: movl	12(%ap), %r2
; CHECK-NEXT: movl	8(%ap), %r1
; CHECK-NEXT: movl	16(%ap), %r3
; CHECK-NEXT: subl3	%r2, %r0, %r0
; CHECK-NEXT: sbwc	%r3, %r1
; CHECK-NEXT: ret
; CHECK-NEXT: .Lfunc_end1:
  %r = sub i64 %a, %b
  ret i64 %r
}

; i64 bitwise AND
define i64 @test_and64(i64 %a, i64 %b) {
; CHECK-LABEL: test_and64:
; CHECK: movl	$-1, %r1
; CHECK-NEXT: xorl3	12(%ap), %r1, %r0
; CHECK-NEXT: bicl3	%r0, 4(%ap), %r0
; CHECK-NEXT: xorl3	16(%ap), %r1, %r1
; CHECK-NEXT: bicl3	%r1, 8(%ap), %r1
; CHECK-NEXT: ret
; CHECK-NEXT: .Lfunc_end2:
  %r = and i64 %a, %b
  ret i64 %r
}

; i64 bitwise OR
define i64 @test_or64(i64 %a, i64 %b) {
; CHECK-LABEL: test_or64:
; CHECK: movl	16(%ap), %r1
; CHECK-NEXT: movl	12(%ap), %r0
; CHECK-NEXT: bisl3	4(%ap), %r0, %r0
; CHECK-NEXT: bisl3	8(%ap), %r1, %r1
; CHECK-NEXT: ret
; CHECK-NEXT: .Lfunc_end3:
  %r = or i64 %a, %b
  ret i64 %r
}

; i64 constant return
define i64 @test_ret_const() {
; CHECK-LABEL: test_ret_const:
; CHECK: movl	$42, %r0
; CHECK-NEXT: clrl	%r1
; CHECK-NEXT: ret
; CHECK-NEXT: .Lfunc_end4:
  ret i64 42
}

; i64 left shift by constant
define i64 @test_shl64(i64 %a) {
; CHECK-LABEL: test_shl64:
; CHECK: movl	4(%ap), %r0
; CHECK-NEXT: movl	8(%ap), %r1
; CHECK-NEXT: ashl	$4, %r1, %r1
; CHECK-NEXT: rotl	$4, %r0, %r2
; CHECK-NEXT: bicl2	$-16, %r2
; CHECK-NEXT: bisl2	%r2, %r1
; CHECK-NEXT: ashl	$4, %r0, %r0
; CHECK-NEXT: ret
; CHECK-NEXT: .Lfunc_end5:
  %r = shl i64 %a, 4
  ret i64 %r
}
