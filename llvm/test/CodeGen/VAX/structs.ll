; RUN: llc -mtriple=vax-unknown-netbsdelf < %s | FileCheck %s

; Test struct/aggregate operations

%struct.Point = type { i32, i32 }
%struct.Rect = type { %struct.Point, %struct.Point }

; Extract from struct
define i32 @extract_field(ptr %p) {
; CHECK-LABEL: extract_field:
; CHECK: movl 4(%ap), %r0
; CHECK: movl 4(%r0), %r0
  %gep = getelementptr %struct.Point, ptr %p, i32 0, i32 1
  %v = load i32, ptr %gep
  ret i32 %v
}

; Store to struct field
define void @store_field(ptr %p, i32 %v) {
; CHECK-LABEL: store_field:
; CHECK: movq 4(%ap), %r0
; CHECK: movl %r1, 4(%r0)
  %gep = getelementptr %struct.Point, ptr %p, i32 0, i32 1
  store i32 %v, ptr %gep
  ret void
}

; Nested struct access
define i32 @nested_struct(ptr %r) {
; CHECK-LABEL: nested_struct:
; CHECK: movl 4(%ap), %r0
; CHECK: movl 12(%r0), %r0
  %gep = getelementptr %struct.Rect, ptr %r, i32 0, i32 1, i32 1
  %v = load i32, ptr %gep
  ret i32 %v
}

; Struct passed by value (byval) — passed as pointer on VAX
define i32 @byval_arg(ptr byval(%struct.Point) %p) {
; CHECK-LABEL: byval_arg:
; CHECK: movl 4(%ap), %r0
; CHECK: movl (%r0), %r0
  %v = load i32, ptr %p
  ret i32 %v
}

; Return aggregate via sret — already covered in sret.ll but add more
define void @init_point(ptr sret(%struct.Point) %ret, i32 %x, i32 %y) {
; CHECK-LABEL: init_point:
  %gep_x = getelementptr %struct.Point, ptr %ret, i32 0, i32 0
  store i32 %x, ptr %gep_x
  %gep_y = getelementptr %struct.Point, ptr %ret, i32 0, i32 1
  store i32 %y, ptr %gep_y
  ret void
}
