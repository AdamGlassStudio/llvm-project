; RUN: llc -mtriple=vax-unknown-netbsdelf -o - %s | FileCheck %s

; Simple call with no args, return value used
define i32 @caller_noargs() {
; CHECK-LABEL: caller_noargs:
; CHECK: .word 0
; CHECK: calls $0, callee_noargs
; CHECK: ret
  %r = call i32 @callee_noargs()
  ret i32 %r
}

declare i32 @callee_noargs()

; Call with two immediate args
define i32 @caller_two_imm() {
; CHECK-LABEL: caller_two_imm:
; CHECK: .word 0
; CHECK: subl2 $8, %sp
; CHECK: movl $32, 4(%sp)
; CHECK: movl $10, (%sp)
; CHECK: calls $2, add_two
; CHECK: addl2 $8, %sp
; CHECK: ret
  %r = call i32 @add_two(i32 10, i32 32)
  ret i32 %r
}

declare i32 @add_two(i32, i32)

; Callee: receives two args via AP, returns sum
define i32 @add_impl(i32 %a, i32 %b) {
; CHECK-LABEL: add_impl:
; CHECK: .word 0
; CHECK-DAG: movl 4(%ap), {{%r[0-9]+}}
; CHECK-DAG: movl 8(%ap), {{%r[0-9]+}}
; CHECK: addl3
; CHECK: ret
  %r = add i32 %a, %b
  ret i32 %r
}

; Call with four args (passed through from caller's args)
define i32 @forward_args(i32 %a, i32 %b) {
; CHECK-LABEL: forward_args:
; CHECK: .word 0
; CHECK: subl2 $16, %sp
; CHECK: calls $4, four_args
; CHECK: addl2 $16, %sp
; CHECK: ret
  %r = call i32 @four_args(i32 %a, i32 %b, i32 %a, i32 %b)
  ret i32 %r
}

declare i32 @four_args(i32, i32, i32, i32)
