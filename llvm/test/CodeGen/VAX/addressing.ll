; RUN: llc -mtriple=vax-unknown-netbsdelf < %s | FileCheck %s

; Test addressing mode patterns

; Register deferred (load through pointer)
define i32 @load_ptr_deref(ptr %p) {
; CHECK-LABEL: load_ptr_deref:
; CHECK: movl 4(%ap), %r0
; CHECK: movl (%r0), %r0
  %v = load i32, ptr %p
  ret i32 %v
}

; Double dereference (pointer to pointer)
define i32 @load_double_deref(ptr %pp) {
; CHECK-LABEL: load_double_deref:
; CHECK: movl 4(%ap), %r0
; CHECK: movl (%r0), %r0
; CHECK: movl (%r0), %r0
  %p = load ptr, ptr %pp
  %v = load i32, ptr %p
  ret i32 %v
}

; Store through pointer
define void @store_through_ptr(ptr %p, i32 %v) {
; CHECK-LABEL: store_through_ptr:
; CHECK: movl {{.*}}, (%r0)
  store i32 %v, ptr %p
  ret void
}

; Array element with variable index (base + scaled index → indexed mode)
define i32 @array_element(ptr %arr, i32 %idx) {
; CHECK-LABEL: array_element:
; CHECK: movl {{.*}}[%r0], %r0
  %gep = getelementptr i32, ptr %arr, i32 %idx
  %v = load i32, ptr %gep
  ret i32 %v
}

; Multiple constant GEPs folded into single displacement
define i32 @multi_gep(ptr %base) {
; CHECK-LABEL: multi_gep:
; CHECK: movl 4(%ap), %r0
; CHECK: movl 24(%r0), %r0
  %g1 = getelementptr i32, ptr %base, i32 4
  %g2 = getelementptr i32, ptr %g1, i32 2
  %v = load i32, ptr %g2
  ret i32 %v
}

; Negative displacement
define i32 @negative_offset(ptr %p) {
; CHECK-LABEL: negative_offset:
; CHECK: movl 4(%ap), %r0
; CHECK: movl -4(%r0), %r0
  %gep = getelementptr i32, ptr %p, i32 -1
  %v = load i32, ptr %gep
  ret i32 %v
}
