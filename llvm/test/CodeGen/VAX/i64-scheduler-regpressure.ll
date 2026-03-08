; RUN: llc -mtriple=vax-unknown-netbsdelf -O2 < %s | FileCheck %s
;
; Regression test for register-pressure scheduler (list-burr) with i64 operations.
;
; History: commit 8fbcb5ad6449 switched to Sched::Source because the list-burr
; scheduler called getMinimalPhysRegClass(R0, MVT::i64), which asserted because
; no GPR class holds i64 — only the QPR paired-register class does, and R0 alone
; is not in QPR. The crash occurred when i64 libcall returns (e.g., __muldi3)
; produced a CopyFromReg typed as MVT::i64 for physical R0.
;
; The fix was two-fold:
;   1. Custom CC lowering (VAXISelLowering.cpp ~line 923/1145) now splits i64
;      returns into two i32 CopyFromReg nodes (R0=lo, R1=hi), so the scheduler
;      never sees (R0, MVT::i64).
;   2. With that fix in place, Sched::RegPressure works correctly and produces
;      better code (fewer spills/reloads).
;
; This test verifies the scheduler handles i64 operations without crashing
; under various register pressure scenarios.

declare void @use_i32(i32)
declare void @use_i64(i64)
declare i64 @get_i64()
declare void @variadic(i32, ...)

; Original crash pattern: or + variadic call + i64 load + i64 mul + ret i64
define i64 @i64_original_crash(ptr %p, i32 %x, i32 %y) {
; CHECK-LABEL: i64_original_crash:
; CHECK: calls
; CHECK: ret
entry:
  %or = or i32 %x, %y
  call void (i32, ...) @variadic(i32 %or, i32 1)
  %val = load i64, ptr %p
  %mul = mul i64 %val, 3
  ret i64 %mul
}

; Multiple i64 multiplies: each generates __muldi3 libcall returning R0:R1.
; Three live i64 values force heavy spilling with register pressure tracking.
define i64 @i64_multi_mul(i64 %a, i64 %b, i64 %c, i32 %x, i32 %y) {
; CHECK-LABEL: i64_multi_mul:
; CHECK: ret
entry:
  %ab = mul i64 %a, %b
  %xy = add i32 %x, %y
  call void @use_i32(i32 %xy)
  %bc = mul i64 %b, %c
  %sum = add i64 %ab, %bc
  %ac = mul i64 %a, %c
  %total = add i64 %sum, %ac
  ret i64 %total
}

; i64 div + mul + rem: three different libcalls (__divdi3, __muldi3, __moddi3),
; all returning via R0:R1, interleaved with i32 use to keep registers live.
define i64 @i64_div_mul_rem(i64 %a, i64 %b, i32 %p, i32 %q, i32 %r) {
; CHECK-LABEL: i64_div_mul_rem:
; CHECK: ret
entry:
  %pq = add i32 %p, %q
  %qr = add i32 %q, %r
  %div = sdiv i64 %a, %b
  call void @use_i32(i32 %pq)
  %mul = mul i64 %div, %a
  call void @use_i32(i32 %qr)
  %rem = srem i64 %mul, %b
  ret i64 %rem
}

; Chain of i64-returning calls with live values across each call.
; Each get_i64() returns R0:R1; scheduler must track pressure across calls.
define i64 @i64_call_chain(i32 %x, i32 %y, i32 %z) {
; CHECK-LABEL: i64_call_chain:
; CHECK: ret
entry:
  %xy = or i32 %x, %y
  %yz = and i32 %y, %z
  %xz = xor i32 %x, %z
  call void @use_i32(i32 %xy)
  call void @use_i32(i32 %yz)
  %v1 = call i64 @get_i64()
  %v2 = call i64 @get_i64()
  call void @use_i32(i32 %xz)
  %mul = mul i64 %v1, %v2
  %v3 = call i64 @get_i64()
  %sum = add i64 %mul, %v3
  ret i64 %sum
}

; Many simultaneous i64 live ranges from loads + multiplies.
define i64 @i64_many_live(ptr %p) {
; CHECK-LABEL: i64_many_live:
; CHECK: ret
entry:
  %a = load i64, ptr %p
  %p1 = getelementptr i64, ptr %p, i32 1
  %b = load i64, ptr %p1
  %p2 = getelementptr i64, ptr %p, i32 2
  %c = load i64, ptr %p2
  %p3 = getelementptr i64, ptr %p, i32 3
  %d = load i64, ptr %p3
  %ab = mul i64 %a, %b
  %cd = mul i64 %c, %d
  %abcd = add i64 %ab, %cd
  %ac = mul i64 %a, %c
  %bd = mul i64 %b, %d
  %acbd = add i64 %ac, %bd
  %result = sub i64 %abcd, %acbd
  ret i64 %result
}
