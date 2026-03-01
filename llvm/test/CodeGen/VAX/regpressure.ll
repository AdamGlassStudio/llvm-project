; RUN: llc -mtriple=vax-unknown-netbsdelf < %s | FileCheck %s

; Test register pressure / spill-reload patterns
; Functions that use many live values simultaneously

define i32 @many_regs(i32 %a, i32 %b, i32 %c, i32 %d) {
; CHECK-LABEL: many_regs:
; CHECK: .word
  %r1 = add i32 %a, %b
  %r2 = sub i32 %c, %d
  %r3 = mul i32 %a, %c
  %r4 = add i32 %b, %d
  %r5 = sub i32 %r1, %r2
  %r6 = add i32 %r3, %r4
  %r7 = mul i32 %r5, %r6
  %r8 = add i32 %r7, %r1
  %r9 = sub i32 %r8, %r2
  %r10 = add i32 %r9, %r3
  ret i32 %r10
}

; Function using many callee-saved registers (should see nonzero entry mask)
define i32 @callee_saved_pressure(i32 %a, i32 %b) {
; CHECK-LABEL: callee_saved_pressure:
; CHECK: .word
  %v1 = add i32 %a, 1
  %v2 = add i32 %b, 2
  %v3 = mul i32 %v1, %v2
  %v4 = add i32 %v3, %a
  %v5 = sub i32 %v4, %b
  %v6 = mul i32 %v5, %v1
  %v7 = add i32 %v6, %v2
  %v8 = sub i32 %v7, %v3
  %v9 = mul i32 %v8, %v4
  %v10 = add i32 %v9, %v5
  %v11 = sub i32 %v10, %v6
  %v12 = mul i32 %v11, %v7
  ret i32 %v12
}

; Function calling other functions — callee-saved regs must survive
declare i32 @external_func(i32)

define i32 @across_call(i32 %a, i32 %b) {
; CHECK-LABEL: across_call:
; CHECK: .word
; CHECK: calls
  %r1 = call i32 @external_func(i32 %a)
  %r2 = add i32 %r1, %b
  %r3 = call i32 @external_func(i32 %r2)
  %r4 = add i32 %r3, %a
  ret i32 %r4
}
