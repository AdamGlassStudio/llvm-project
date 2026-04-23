; RUN: llc -mtriple=vax-unknown-netbsdelf -global-isel -global-isel-abort=2 < %s 2>&1 | FileCheck %s

; LSHR on i64 is intentionally NOT supported by GISel (LegalizerHelper
; has no shift-libcall dispatch in getRTLibDesc, and inline expansion
; is ~30 instructions). It must fall back to SDAG, which emits the
; __lshrdi3 libcall like GCC. -global-isel-abort=2 allows the fallback
; and emits a warning so we assert both the fallback behavior and the
; resulting code.

; CHECK: warning: Instruction selection used fallback path for gisel_i64_lshr

define i64 @gisel_i64_lshr(i64 %a, i32 %s) {
; CHECK-LABEL: gisel_i64_lshr:
; CHECK: calls {{.*}}__lshrdi3
; CHECK: ret
  %c = zext i32 %s to i64
  %r = lshr i64 %a, %c
  ret i64 %r
}
