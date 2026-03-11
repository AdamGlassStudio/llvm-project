; RUN: llc -mtriple=vax-unknown-netbsdelf < %s | FileCheck %s

; Test memory operations and bit manipulation patterns

declare void @llvm.memcpy.p0.p0.i32(ptr, ptr, i32, i1)
declare void @llvm.memset.p0.i32(ptr, i8, i32, i1)

; Small memcpy — inlined as movl sequence (VAX allows unaligned access)
define void @small_memcpy(ptr %dst, ptr %src) {
; CHECK-LABEL: small_memcpy:
; CHECK-NOT: calls $3, memcpy
; CHECK: movl
; CHECK: ret
  call void @llvm.memcpy.p0.p0.i32(ptr %dst, ptr %src, i32 16, i1 false)
  ret void
}

; Small memset — inlined as clrq sequence (VAX allows unaligned access)
define void @small_memset(ptr %dst) {
; CHECK-LABEL: small_memset:
; CHECK-NOT: calls $3, memset
; CHECK: clrq
; CHECK: ret
  call void @llvm.memset.p0.i32(ptr %dst, i8 0, i32 16, i1 false)
  ret void
}

; Byte rotate pattern — VAX has native ROTL instruction
define i32 @byte_rotate(i32 %x) {
; CHECK-LABEL: byte_rotate:
; CHECK: rotl $8
  %s1 = shl i32 %x, 8
  %s2 = lshr i32 %x, 24
  %r = or i32 %s1, %s2
  ret i32 %r
}

; Rotate by 16 bits
define i32 @half_rotate(i32 %x) {
; CHECK-LABEL: half_rotate:
; CHECK: rotl $16
  %s1 = shl i32 %x, 16
  %s2 = lshr i32 %x, 16
  %r = or i32 %s1, %s2
  ret i32 %r
}
