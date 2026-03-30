; RUN: llc -mtriple=vax-unknown-netbsdelf < %s | FileCheck %s

; FastCC leaf function: no entry mask, JSB/RSB protocol.
define internal fastcc i32 @leaf_add(i32 %a, i32 %b) {
; CHECK-LABEL: leaf_add:
; CHECK-NOT: .word
; CHECK: pushl %fp
; CHECK-NEXT: movl %sp, %fp
; CHECK: 8(%fp)
; CHECK: movl (%fp), %fp
; CHECK-NEXT: addl2 $4, %sp
; CHECK-NEXT: rsb
entry:
  %sum = add i32 %a, %b
  ret i32 %sum
}

; FastCC caller: uses jsb instead of calls, caller pops args.
define i32 @test_fastcc_call(i32 %x, i32 %y) {
; CHECK-LABEL: test_fastcc_call:
; CHECK: .word
; CHECK: pushl
; CHECK: pushl
; CHECK: jsb leaf_add
; CHECK-NEXT: addl2 $8, %sp
; CHECK: ret
entry:
  %r = call fastcc i32 @leaf_add(i32 %x, i32 %y)
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
