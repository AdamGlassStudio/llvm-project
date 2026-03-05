; RUN: llc -mtriple=vax-unknown-netbsdelf -O2 < %s | FileCheck %s
;
; Regression test for scheduler crash with i64 operations at -O2.
; The register-pressure scheduler (list-burr) called getMinimalPhysRegClass
; with (R0, MVT::i64), which has no matching register class on VAX (32-bit).
; Fixed by switching to source-order scheduling (Sched::Source).

declare void @variadic(i32, ...)

; This pattern triggered the crash: or i32 + variadic call + load i64 +
; mul i64 + ret i64. The combination creates enough register pressure to
; expose the scheduler bug.
define i64 @i64_complex(ptr %p, i32 %x, i32 %y) {
; CHECK-LABEL: i64_complex:
; CHECK: calls
; CHECK: ret
entry:
  %or = or i32 %x, %y
  call void (i32, ...) @variadic(i32 %or, i32 1)
  %val = load i64, ptr %p
  %mul = mul i64 %val, 3
  ret i64 %mul
}
