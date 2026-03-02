; RUN: llc -mtriple=vax-unknown-netbsdelf < %s | FileCheck %s
;
; Test i64 calling convention: arguments passed on stack, return in R0:R1.

; Pass i64 argument to a function.
declare void @consume_i64(i64)

define void @pass_i64(i64 %x) {
; CHECK-LABEL: pass_i64:
; i64 arg pushed as two longwords.
; CHECK: pushl
; CHECK: pushl
; CHECK: calls $2, consume_i64
  call void @consume_i64(i64 %x)
  ret void
}

; Return i64 value: low half in R0, high half in R1.
define i64 @ret_i64_const() {
; CHECK-LABEL: ret_i64_const:
; CHECK-DAG: movl $42, %r0
; CHECK-DAG: clrl %r1
; CHECK: ret
  ret i64 42
}

; Call a function returning i64 and use the result.
declare i64 @produce_i64()

define i64 @call_ret_i64() {
; CHECK-LABEL: call_ret_i64:
; CHECK: calls $0, produce_i64
; Result comes back in R0:R1.
; CHECK: ret
  %r = call i64 @produce_i64()
  ret i64 %r
}

; Pass two i64 arguments.
declare i64 @add_i64(i64, i64)

define i64 @pass_two_i64(i64 %a, i64 %b) {
; CHECK-LABEL: pass_two_i64:
; Both i64 args on stack (4 words total: a_lo, a_hi, b_lo, b_hi).
; CHECK: calls $4, add_i64
; CHECK: ret
  %r = call i64 @add_i64(i64 %a, i64 %b)
  ret i64 %r
}

; Mixed i32 and i64 arguments.
declare i64 @mixed_args(i32, i64, i32)

define i64 @pass_mixed(i32 %a, i64 %b, i32 %c) {
; CHECK-LABEL: pass_mixed:
; CHECK: calls $4, mixed_args
; CHECK: ret
  %r = call i64 @mixed_args(i32 %a, i64 %b, i32 %c)
  ret i64 %r
}

; i64 multiply using EMUL.
define i64 @i64_mul(i32 %a, i32 %b) {
; CHECK-LABEL: i64_mul:
; Sign-extend multiply: (sext a) * (sext b) uses EMUL.
; CHECK: emul
; CHECK: ret
  %ea = sext i32 %a to i64
  %eb = sext i32 %b to i64
  %r = mul i64 %ea, %eb
  ret i64 %r
}
