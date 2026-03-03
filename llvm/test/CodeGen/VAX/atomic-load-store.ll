; RUN: llc -mtriple=vax-unknown-netbsdelf < %s | FileCheck %s

; Test atomic load and store for i32.
; VAX has no store buffer or OOO, so atomics are plain MOVL.

define i32 @atomic_load_i32(ptr %p) {
; CHECK-LABEL: atomic_load_i32:
; CHECK: movl
  %v = load atomic i32, ptr %p seq_cst, align 4
  ret i32 %v
}

define void @atomic_store_i32(ptr %p, i32 %v) {
; CHECK-LABEL: atomic_store_i32:
; CHECK: movl
  store atomic i32 %v, ptr %p seq_cst, align 4
  ret void
}

define i32 @atomic_load_acquire(ptr %p) {
; CHECK-LABEL: atomic_load_acquire:
; CHECK: movl
  %v = load atomic i32, ptr %p acquire, align 4
  ret i32 %v
}

define void @atomic_store_release(ptr %p, i32 %v) {
; CHECK-LABEL: atomic_store_release:
; CHECK: movl
  store atomic i32 %v, ptr %p release, align 4
  ret void
}
