; RUN: llc -mtriple=vax-unknown-netbsdelf < %s | FileCheck %s
;
; Test calling functions with f64 (D_float) arguments.
; Regression test: PUSHL only handled i32; f64 args need two pushes.

declare double @bar(double, double)

define double @call_f64(double %a, double %b) {
; CHECK-LABEL: call_f64:
; CHECK: pushl
; CHECK: pushl
; CHECK: pushl
; CHECK: pushl
; CHECK: calls $2
; CHECK: ret
  %r = call double @bar(double %a, double %b)
  ret double %r
}

declare double @baz(i32, double)

define double @call_mixed(i32 %x, double %d) {
; CHECK-LABEL: call_mixed:
; CHECK: pushl
; CHECK: pushl
; CHECK: pushl
; CHECK: calls $2
; CHECK: ret
  %r = call double @baz(i32 %x, double %d)
  ret double %r
}
