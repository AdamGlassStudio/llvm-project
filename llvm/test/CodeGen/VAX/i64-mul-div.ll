; RUN: llc -mtriple=vax-unknown-netbsdelf < %s | FileCheck %s

; Test 64-bit multiply and divide operations.

define i64 @mul_i64(i64 %a, i64 %b) {
; CHECK-LABEL: mul_i64:
; CHECK: emul
; CHECK-NOT: calls {{.*}}, __muldi3
  %r = mul i64 %a, %b
  ret i64 %r
}

define i64 @sdiv_i64(i64 %a, i64 %b) {
; CHECK-LABEL: sdiv_i64:
; CHECK: calls
  %r = sdiv i64 %a, %b
  ret i64 %r
}

define i64 @udiv_i64(i64 %a, i64 %b) {
; CHECK-LABEL: udiv_i64:
; CHECK: calls
  %r = udiv i64 %a, %b
  ret i64 %r
}

define i64 @srem_i64(i64 %a, i64 %b) {
; CHECK-LABEL: srem_i64:
; CHECK: calls
  %r = srem i64 %a, %b
  ret i64 %r
}
