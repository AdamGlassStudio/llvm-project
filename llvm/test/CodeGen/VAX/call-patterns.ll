; RUN: llc -mtriple=vax-unknown-netbsdelf < %s | FileCheck %s

; Test function call patterns

declare i32 @external_func(i32)
declare void @void_func()
declare i32 @multi_arg(i32, i32, i32, i32)

; Call with one argument
define i32 @call_one_arg(i32 %x) {
; CHECK-LABEL: call_one_arg:
; CHECK: pushl
; CHECK: calls $1, external_func
  %r = call i32 @external_func(i32 %x)
  ret i32 %r
}

; Call with no arguments, void return
define void @call_void() {
; CHECK-LABEL: call_void:
; CHECK: calls $0, void_func
  call void @void_func()
  ret void
}

; Call with four arguments
define i32 @call_four_args(i32 %a, i32 %b, i32 %c, i32 %d) {
; CHECK-LABEL: call_four_args:
; CHECK: calls $4, multi_arg
  %r = call i32 @multi_arg(i32 %a, i32 %b, i32 %c, i32 %d)
  ret i32 %r
}

; Chained calls — result of first feeds second
define i32 @call_chain(i32 %x) {
; CHECK-LABEL: call_chain:
; CHECK: calls $1, external_func
; CHECK: calls $1, external_func
  %r1 = call i32 @external_func(i32 %x)
  %r2 = call i32 @external_func(i32 %r1)
  ret i32 %r2
}

; Indirect call through function pointer
define i32 @indirect_call(ptr %f, i32 %x) {
; CHECK-LABEL: indirect_call:
; CHECK: calls $1, (%r0)
  %r = call i32 %f(i32 %x)
  ret i32 %r
}
