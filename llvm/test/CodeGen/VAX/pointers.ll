; RUN: llc -mtriple=vax-unknown-netbsdelf < %s | FileCheck %s

; Test pointer operations

@array = external global [10 x i32]

; Pointer arithmetic
define ptr @ptr_add(ptr %p, i32 %idx) {
; CHECK-LABEL: ptr_add:
  %r = getelementptr i32, ptr %p, i32 %idx
  ret ptr %r
}

; Function pointer call
define i32 @indirect_call(ptr %fp, i32 %arg) {
; CHECK-LABEL: indirect_call:
; CHECK: calls
  %r = call i32 %fp(i32 %arg)
  ret i32 %r
}

; Null pointer check
define i32 @null_check(ptr %p) {
; CHECK-LABEL: null_check:
; CHECK: movl 4(%ap), %r0
; CHECK-NEXT: beql
entry:
  %isnull = icmp eq ptr %p, null
  br i1 %isnull, label %is_null, label %not_null

is_null:
  ret i32 0

not_null:
  %v = load i32, ptr %p
  ret i32 %v
}

; Pointer subtraction (ptrtoint-based)
define i32 @ptr_diff(ptr %a, ptr %b) {
; CHECK-LABEL: ptr_diff:
; CHECK: subl
  %ia = ptrtoint ptr %a to i32
  %ib = ptrtoint ptr %b to i32
  %diff = sub i32 %ia, %ib
  ret i32 %diff
}

; Load through double indirection (pointer to pointer)
define i32 @double_indirect(ptr %pp) {
; CHECK-LABEL: double_indirect:
  %p = load ptr, ptr %pp
  %v = load i32, ptr %p
  ret i32 %v
}
