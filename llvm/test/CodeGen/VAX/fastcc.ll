; RUN: llc -mtriple=vax-unknown-netbsdelf < %s | FileCheck %s

; FastCC leaf function with register args: no entry mask, JSB/RSB protocol.
; Args arrive in R0 and R1 (first 4 i32 args use registers).
define internal fastcc i32 @leaf_add(i32 %a, i32 %b) {
; CHECK-LABEL: leaf_add:
; CHECK-NOT: .word
; CHECK: pushl %fp
; CHECK-NEXT: movl %sp, %fp
; CHECK: addl2 %r1, %r0
; CHECK: movl (%fp), %fp
; CHECK-NEXT: addl2 $4, %sp
; CHECK-NEXT: rsb
entry:
  %sum = add i32 %a, %b
  ret i32 %sum
}

; FastCC caller: uses jsb instead of calls, no stack arg push needed.
define i32 @test_fastcc_call(i32 %x, i32 %y) {
; CHECK-LABEL: test_fastcc_call:
; CHECK: .word
; CHECK: jsb leaf_add
; CHECK-NOT: addl2 ${{[0-9]+}}, %sp
; CHECK: ret
entry:
  %r = call fastcc i32 @leaf_add(i32 %x, i32 %y)
  ret i32 %r
}

; FastCC with 5 args: first 4 in regs, 5th on stack.
define internal fastcc i32 @five_args(i32 %a, i32 %b, i32 %c, i32 %d, i32 %e) {
; CHECK-LABEL: five_args:
; CHECK-NOT: .word
; CHECK: pushl %fp
; CHECK: 8(%fp)
; CHECK: rsb
entry:
  %s1 = add i32 %a, %b
  %s2 = add i32 %s1, %c
  %s3 = add i32 %s2, %d
  %s4 = add i32 %s3, %e
  ret i32 %s4
}

; Caller with 5 args: 5th pushed to stack, cleanup after jsb.
define i32 @test_five_args(i32 %x) {
; CHECK-LABEL: test_five_args:
; CHECK: .word
; CHECK: pushl
; CHECK: jsb five_args
; CHECK: addl2 $4, %sp
; CHECK: ret
entry:
  %r = call fastcc i32 @five_args(i32 %x, i32 %x, i32 %x, i32 %x, i32 %x)
  ret i32 %r
}

; Standard CC function: entry mask and ret.
define i32 @standard_cc(i32 %a) {
; CHECK-LABEL: standard_cc:
; CHECK: .word
; CHECK: ret
; CHECK-NOT: rsb
entry:
  ret i32 %a
}
