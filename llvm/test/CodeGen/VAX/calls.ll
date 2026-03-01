; RUN: llc -mtriple=vax-unknown-netbsdelf -o - %s | FileCheck %s

; Simple call with no args, return value used
define i32 @caller_noargs() {
; CHECK-LABEL: caller_noargs:
; CHECK: .word 0
; CHECK: calls $0, callee_noargs
; CHECK-NEXT: ret
  %r = call i32 @callee_noargs()
  ret i32 %r
}

declare i32 @callee_noargs()

; Call with two immediate args — CALLS/RET cleans args, no addl2 after.
define i32 @caller_two_imm() {
; CHECK-LABEL: caller_two_imm:
; CHECK: .word 0
; CHECK: subl2 $8, %sp
; CHECK: movl $32, 4(%sp)
; CHECK: movl $10, (%sp)
; CHECK: calls $2, add_two
; CHECK-NOT: addl2
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

; Multi-call: result of first call feeds into second.
; Verify no spurious SP adjustments between calls.
define i32 @multi_call() {
; CHECK-LABEL: multi_call:
; CHECK: .word 0
; CHECK: calls $0, get_val
; CHECK-NOT: addl2
; CHECK: subl2 $4, %sp
; CHECK: calls $1, inc_val
; CHECK-NOT: addl2
; CHECK: ret
  %a = call i32 @get_val()
  %b = call i32 @inc_val(i32 %a)
  ret i32 %b
}

declare i32 @get_val()
declare i32 @inc_val(i32)

; Function with local variable: prologue allocates frame, local at FP-relative.
define i32 @with_local(i32 %a) {
; CHECK-LABEL: with_local:
; CHECK: .word 0
; CHECK: subl2 $4, %sp
; CHECK: movl 4(%ap), %r0
; CHECK: movl %r0, -4(%fp)
  %p = alloca i32
  store i32 %a, ptr %p
  %v = load i32, ptr %p
  %r = add i32 %v, 1
  ret i32 %r
}
