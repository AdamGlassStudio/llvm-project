; RUN: llc -mtriple=vax-unknown-netbsdelf -O2 < %s | FileCheck %s
;
; Test MOVQ/CLRQ peephole: adjacent MOVL pairs → MOVQ, CLRL pairs → CLRQ.
; The peephole fires when two MOVL loads/stores target adjacent memory
; and use consecutive registers (Rn, Rn+1).

; --- CLRQ: store i64 0 → clrq ---

define void @zero_i64(ptr %p) {
; CHECK-LABEL: zero_i64:
; CHECK: clrq (%r0)
; CHECK: ret
  store i64 0, ptr %p
  ret void
}

define void @zero_two_i64(ptr %p) {
; CHECK-LABEL: zero_two_i64:
; CHECK: clrq 8(%r0)
; CHECK: clrq (%r0)
; CHECK: ret
  store i64 0, ptr %p
  %p2 = getelementptr i64, ptr %p, i32 1
  store i64 0, ptr %p2
  ret void
}

define void @store_zero_disp(ptr %p) {
; CHECK-LABEL: store_zero_disp:
; CHECK: clrq 16(%r0)
; CHECK: ret
  %gep = getelementptr i64, ptr %p, i32 2
  store i64 0, ptr %gep
  ret void
}

; --- MOVQ load: two MOVL_rm from adjacent mem into Rn:Rn+1 → movq ---

define i64 @load_i64(ptr %p) {
; CHECK-LABEL: load_i64:
; CHECK: movq (%r1), %r0
; CHECK: ret
  %v = load i64, ptr %p
  ret i64 %v
}

define i64 @load_i64_disp(ptr %p) {
; CHECK-LABEL: load_i64_disp:
; CHECK: movq 24(%r1), %r0
; CHECK: ret
  %gep = getelementptr i64, ptr %p, i32 3
  %v = load i64, ptr %gep
  ret i64 %v
}

; i64 argument from stack → movq loads both halves
define i64 @return_i64_arg(i64 %x) {
; CHECK-LABEL: return_i64_arg:
; CHECK: movq 4(%ap), %r0
; CHECK: ret
  ret i64 %x
}

; Two pointer args: load from src, store to dst
define void @copy_i64(ptr %dst, ptr %src) {
; CHECK-LABEL: copy_i64:
; CHECK: movl 8(%ap), %r0
; CHECK: movl (%r0), %r1
; CHECK: movl 4(%r0), %r0
; CHECK: movl 4(%ap), %r2
; CHECK: movl %r0, 4(%r2)
; CHECK: movl %r1, (%r2)
; CHECK: ret
  %v = load i64, ptr %src
  store i64 %v, ptr %dst
  ret void
}

; --- Store: MOVL pair stores may not combine (register allocator ---
; --- doesn't guarantee consecutive regs for arbitrary values)     ---

; Store of a non-zero constant i64 — halves go through separate regs,
; register allocator may or may not assign consecutive pair.
define void @store_const_i64(ptr %p) {
; CHECK-LABEL: store_const_i64:
; CHECK: movl $256, 4(%r0)
; CHECK: clrl (%r0)
; CHECK: ret
  store i64 1099511627776, ptr %p  ; 0x100_0000_0000
  ret void
}
