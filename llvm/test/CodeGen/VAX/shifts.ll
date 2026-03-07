; RUN: llc -march=vax < %s | FileCheck %s

; Left shift by variable
define i32 @test_shl(i32 %a, i32 %b) {
; CHECK-LABEL: test_shl:
; CHECK: movl	4(%ap), %r0
; CHECK-NEXT: movl	8(%ap), %r1
; CHECK-NEXT: ashl	%r1, %r0, %r0
; CHECK-NEXT: ret
; CHECK-NEXT: .Lfunc_end0:
  %r = shl i32 %a, %b
  ret i32 %r
}

; Left shift by constant
define i32 @test_shl_const(i32 %a) {
; CHECK-LABEL: test_shl_const:
; CHECK: movl	4(%ap), %r0
; CHECK-NEXT: ashl	$4, %r0, %r0
; CHECK-NEXT: ret
; CHECK-NEXT: .Lfunc_end1:
  %r = shl i32 %a, 4
  ret i32 %r
}

; Arithmetic right shift by variable
define i32 @test_ashr(i32 %a, i32 %b) {
; CHECK-LABEL: test_ashr:
; CHECK: movl	4(%ap), %r0
; CHECK-NEXT: clrl	%r1
; CHECK-NEXT: subl3	8(%ap), %r1, %r1
; CHECK-NEXT: ashl	%r1, %r0, %r0
; CHECK-NEXT: ret
; CHECK-NEXT: .Lfunc_end2:
  %r = ashr i32 %a, %b
  ret i32 %r
}

; Arithmetic right shift by constant
define i32 @test_ashr_const(i32 %a) {
; CHECK-LABEL: test_ashr_const:
; CHECK: movl	4(%ap), %r0
; CHECK-NEXT: ashl	$-4, %r0, %r0
; CHECK-NEXT: ret
; CHECK-NEXT: .Lfunc_end3:
  %r = ashr i32 %a, 4
  ret i32 %r
}

; Logical right shift by constant
define i32 @test_lshr_const(i32 %a) {
; CHECK-LABEL: test_lshr_const:
; CHECK: movl	4(%ap), %r0
; CHECK-NEXT: rotl	$28, %r0, %r0
; CHECK-NEXT: bicl2	$-268435456, %r0
; CHECK-NEXT: ret
; CHECK-NEXT: .Lfunc_end4:
  %r = lshr i32 %a, 4
  ret i32 %r
}

; Logical right shift by variable
define i32 @test_lshr(i32 %a, i32 %b) {
; CHECK-LABEL: test_lshr:
; CHECK: movl	4(%ap), %r0
; CHECK-NEXT: movl	8(%ap), %r1
; CHECK-NEXT: mnegl	%r1, %r2
; CHECK-NEXT: ashl	%r2, %r0, %r0
; CHECK-NEXT: subl3	%r1, $32, %r1
; CHECK-NEXT: movl	$-1, %r2
; CHECK-NEXT: ashl	%r1, %r2, %r1
; CHECK-NEXT: bicl2	%r1, %r0
; CHECK-NEXT: ret
; CHECK-NEXT: .Lfunc_end5:
  %r = lshr i32 %a, %b
  ret i32 %r
}

; Rotate left
define i32 @test_rotl(i32 %a, i32 %b) {
; CHECK-LABEL: test_rotl:
; CHECK: movl	4(%ap), %r0
; CHECK-NEXT: movl	8(%ap), %r1
; CHECK-NEXT: rotl	%r1, %r0, %r0
; CHECK-NEXT: ret
; CHECK-NEXT: .Lfunc_end6:
  %shl = shl i32 %a, %b
  %sub = sub i32 32, %b
  %shr = lshr i32 %a, %sub
  %r = or i32 %shl, %shr
  ret i32 %r
}
