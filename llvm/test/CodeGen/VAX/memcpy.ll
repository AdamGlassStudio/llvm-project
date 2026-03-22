; RUN: llc -mtriple=vax-unknown-netbsdelf -O2 < %s | FileCheck %s

; Test that memcpy is lowered to MOVC3 for sizes above the inline threshold.
; MaxStoresPerMemcpy=6, so aligned copies > 24 bytes use MOVC3.

declare void @llvm.memcpy.p0.p0.i32(ptr nocapture writeonly, ptr nocapture readonly, i32, i1 immarg)
declare void @llvm.memcpy.p0.p0.i64(ptr nocapture writeonly, ptr nocapture readonly, i64, i1 immarg)

; --- Constant-size copies that should use MOVC3 ---

define void @memcpy_28(ptr %dst, ptr %src) {
; CHECK-LABEL: memcpy_28:
; CHECK: movc3 $28,
; CHECK-NOT: calls
  call void @llvm.memcpy.p0.p0.i32(ptr align 4 %dst, ptr align 4 %src, i32 28, i1 false)
  ret void
}

define void @memcpy_100(ptr %dst, ptr %src) {
; CHECK-LABEL: memcpy_100:
; CHECK: movc3 $100,
; CHECK-NOT: calls
  call void @llvm.memcpy.p0.p0.i32(ptr %dst, ptr %src, i32 100, i1 false)
  ret void
}

define void @memcpy_65535(ptr %dst, ptr %src) {
; CHECK-LABEL: memcpy_65535:
; CHECK: movc3 $65535,
; CHECK-NOT: calls
  call void @llvm.memcpy.p0.p0.i32(ptr %dst, ptr %src, i32 65535, i1 false)
  ret void
}

; --- Constant-size copies that should NOT use MOVC3 ---

; Below threshold (≤ 24 bytes with alignment 4 = ≤ 6 stores): inlined.
define void @memcpy_small(ptr %dst, ptr %src) {
; CHECK-LABEL: memcpy_small:
; CHECK-NOT: movc3
  call void @llvm.memcpy.p0.p0.i32(ptr align 4 %dst, ptr align 4 %src, i32 16, i1 false)
  ret void
}

; Above MOVC3 max (> 65535): should use memcpy libcall.
define void @memcpy_too_large(ptr %dst, ptr %src) {
; CHECK-LABEL: memcpy_too_large:
; CHECK-NOT: movc3
; CHECK: calls
  call void @llvm.memcpy.p0.p0.i32(ptr %dst, ptr %src, i32 65536, i1 false)
  ret void
}

; Volatile: should not use MOVC3.
define void @memcpy_volatile(ptr %dst, ptr %src) {
; CHECK-LABEL: memcpy_volatile:
; CHECK-NOT: movc3
  call void @llvm.memcpy.p0.p0.i32(ptr %dst, ptr %src, i32 100, i1 true)
  ret void
}

; --- Variable-size copy: should use memcpy libcall (can't prove fits in 16 bits) ---

define void @memcpy_dynamic(ptr %dst, ptr %src, i32 %n) {
; CHECK-LABEL: memcpy_dynamic:
; CHECK-NOT: movc3
; CHECK: calls
  call void @llvm.memcpy.p0.p0.i32(ptr %dst, ptr %src, i32 %n, i1 false)
  ret void
}

; --- Struct copy (common pattern, 48 bytes > threshold) ---

%struct.big = type { [48 x i8] }

define void @struct_copy(ptr %dst, ptr %src) {
; CHECK-LABEL: struct_copy:
; CHECK: movc3 $48,
  call void @llvm.memcpy.p0.p0.i32(ptr align 4 %dst, ptr align 4 %src, i32 48, i1 false)
  ret void
}

; --- Register clobber test: values live across MOVC3 must be preserved ---

declare i32 @use(i32)

define i32 @clobber_test(ptr %dst, ptr %src, i32 %val) {
; CHECK-LABEL: clobber_test:
; CHECK: movc3 $48,
; The compiler must save %val across the MOVC3 (which clobbers R0-R5).
  call void @llvm.memcpy.p0.p0.i32(ptr align 4 %dst, ptr align 4 %src, i32 48, i1 false)
  %result = call i32 @use(i32 %val)
  ret i32 %result
}

; --- i64 size intrinsic: should truncate to i32 and use MOVC3 ---

define void @memcpy_i64_size(ptr %dst, ptr %src) {
; CHECK-LABEL: memcpy_i64_size:
; CHECK: movc3 $100,
; CHECK-NOT: calls
  call void @llvm.memcpy.p0.p0.i64(ptr %dst, ptr %src, i64 100, i1 false)
  ret void
}
