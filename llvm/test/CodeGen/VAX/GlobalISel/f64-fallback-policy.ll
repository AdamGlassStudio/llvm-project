; RUN: llc -mtriple=vax-unknown-netbsdelf -global-isel -global-isel-abort=2 < %s 2>&1 | FileCheck %s

; VAX has native hardware FP instructions (ADDD/SUBD/MULD/DIVD) that
; operate on D_float directly, and SDAG selects them. GISel *could*
; reach the same quality by selecting those same instructions — there
; is nothing fundamentally wrong with f64 under GISel. The hazard is
; the libcall fallback: compiler-rt's __adddf3 etc. are IEEE 754 and
; would silently corrupt D_float values. No native FP selectors are
; wired up in the GISel pipeline yet, so any f64 op that reached it
; would risk that libcall path.
;
; Until native FP selectors exist (and a policy of "never libcall to
; compiler-rt for FP" is enforced), VAXCallLowering rejects f64 args
; and returns so the function falls back to SDAG, which is known good.
; This test locks that policy in.

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
