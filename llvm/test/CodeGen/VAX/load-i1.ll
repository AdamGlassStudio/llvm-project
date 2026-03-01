; RUN: llc -mtriple=vax-unknown-netbsdelf < %s | FileCheck %s
;
; Test i1 (C _Bool) loads. LLVM represents _Bool as i1.
; Regression test: load anyext i1 was not handled.

@flag = external global i1

define i32 @load_bool() {
; CHECK-LABEL: load_bool:
; CHECK: movzbl
; CHECK: ret
  %v = load i1, ptr @flag
  %r = zext i1 %v to i32
  ret i32 %r
}
