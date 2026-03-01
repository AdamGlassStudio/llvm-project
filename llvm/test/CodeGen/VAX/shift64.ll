; RUN: llc -mtriple=vax-unknown-netbsdelf < %s | FileCheck %s
;
; Test 64-bit shifts (SHL_PARTS, SRL_PARTS, SRA_PARTS).
; Regression test: SHL_PARTS was not expanded.

define i64 @shl64(i64 %a, i32 %n) {
; CHECK-LABEL: shl64:
; CHECK: ret
  %ext = zext i32 %n to i64
  %r = shl i64 %a, %ext
  ret i64 %r
}

define i64 @lshr64(i64 %a, i32 %n) {
; CHECK-LABEL: lshr64:
; CHECK: ret
  %ext = zext i32 %n to i64
  %r = lshr i64 %a, %ext
  ret i64 %r
}

define i64 @ashr64(i64 %a, i32 %n) {
; CHECK-LABEL: ashr64:
; CHECK: ret
  %ext = zext i32 %n to i64
  %r = ashr i64 %a, %ext
  ret i64 %r
}
