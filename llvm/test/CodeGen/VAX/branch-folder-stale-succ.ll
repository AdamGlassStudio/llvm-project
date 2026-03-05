; RUN: llc -march=vax -O2 < %s | FileCheck %s
; Regression test: fcmp uno folds to false on VAX (no NaN), producing stale
; successor edges. The stale successor cleanup must happen before BranchFolding
; to avoid cascading dead-block removal that deletes reachable code.

; This is a simplified version of the pattern from NetBSD libm's casinh().

define i32 @test_stale_succ(double %x, double %y) {
; CHECK-LABEL: test_stale_succ:
; The normal path (cmpd + conditional branch) must survive.
; CHECK: cmpd
; CHECK: ret
entry:
  %cmp = fcmp uno double %x, 0.0
  %cmp2 = fcmp uno double %y, 0.0
  %or = select i1 %cmp, i1 true, i1 %cmp2
  br i1 %or, label %nan_path, label %normal_path

nan_path:
  ret i32 -1

normal_path:
  %gt = fcmp ogt double %x, %y
  br i1 %gt, label %x_bigger, label %y_bigger

x_bigger:
  ret i32 1

y_bigger:
  ret i32 2
}
