; RUN: llc -mtriple=vax-unknown-netbsdelf -global-isel -global-isel-abort=2 < %s 2>&1 | FileCheck %s

; VAX uses D_float for `double`, NOT IEEE 754. Standard compiler-rt
; libcalls (__adddf3, __muldf3, etc.) operate on IEEE bits and would
; produce wrong results on D_float values. VAXCallLowering therefore
; rejects f64 returns and arguments, forcing the function to fall back
; to SDAG, which handles D_float correctly via QPR + custom lowering.
;
; This test locks in that policy decision: f64 arithmetic MUST NOT go
; through the GISel path.

; CHECK: warning: Instruction selection used fallback path for gisel_f64_add
; CHECK: warning: Instruction selection used fallback path for gisel_f64_mul

define double @gisel_f64_add(double %a, double %b) {
; CHECK-LABEL: gisel_f64_add:
; CHECK: addd
; CHECK: ret
  %r = fadd double %a, %b
  ret double %r
}

define double @gisel_f64_mul(double %a, double %b) {
; CHECK-LABEL: gisel_f64_mul:
; CHECK: muld
; CHECK: ret
  %r = fmul double %a, %b
  ret double %r
}
