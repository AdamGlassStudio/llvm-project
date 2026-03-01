; RUN: llc -mtriple=vax-unknown-netbsdelf < %s | FileCheck %s

; Test recursive calls and global variable access

; Recursive factorial
define i32 @factorial(i32 %n) {
; CHECK-LABEL: factorial:
; CHECK: calls $1, factorial
entry:
  %cmp = icmp sle i32 %n, 1
  br i1 %cmp, label %base, label %rec
base:
  ret i32 1
rec:
  %n1 = sub i32 %n, 1
  %r = call i32 @factorial(i32 %n1)
  %result = mul i32 %n, %r
  ret i32 %result
}

; Global variable read
@counter = external global i32

define i32 @read_global() {
; CHECK-LABEL: read_global:
; CHECK: movl counter, %r0
  %v = load i32, ptr @counter
  ret i32 %v
}

; Global variable write
define void @write_global(i32 %v) {
; CHECK-LABEL: write_global:
; CHECK: movl 4(%ap), %r0
; CHECK: movl %r0, counter
  store i32 %v, ptr @counter
  ret void
}

; Increment global — add 1 optimized to incl
define i32 @increment_global() {
; CHECK-LABEL: increment_global:
; CHECK: movl counter, %r0
; CHECK: incl %r0
; CHECK: movl %r0, counter
  %v = load i32, ptr @counter
  %v1 = add i32 %v, 1
  store i32 %v1, ptr @counter
  ret i32 %v1
}

; Multi-return via struct (divmod)
define { i32, i32 } @divmod(i32 %a, i32 %b) {
; CHECK-LABEL: divmod:
; CHECK: divl3
  %q = sdiv i32 %a, %b
  %r = srem i32 %a, %b
  %v0 = insertvalue { i32, i32 } undef, i32 %q, 0
  %v1 = insertvalue { i32, i32 } %v0, i32 %r, 1
  ret { i32, i32 } %v1
}
