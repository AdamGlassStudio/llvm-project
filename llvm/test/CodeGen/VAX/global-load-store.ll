; RUN: llc -mtriple=vax-unknown-netbsdelf < %s | FileCheck %s

@g = global i32 0

; CHECK-LABEL: load_g:
; CHECK: movl g, %r0
; CHECK: ret
define i32 @load_g() {
  %v = load i32, ptr @g
  ret i32 %v
}
