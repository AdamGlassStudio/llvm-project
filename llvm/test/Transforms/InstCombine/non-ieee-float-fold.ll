; RUN: opt -S -passes=instcombine %s | FileCheck %s
;
; Verify that on targets with non-IEEE floating-point (nif in datalayout),
; loading a double from constant integer data is NOT constant-folded.
; The raw bytes may represent a native float format (e.g., VAX D_float)
; that differs from the IEEE 754 interpretation LLVM would otherwise use.

; DataLayout with "nif" = non-IEEE float
target datalayout = "e-m:e-p:32:32-i1:8:32-i8:8:32-i16:16:32-i64:32-f64:32-a:0:32-n32-nif"

; This mimics the NetBSD libm vc() macro pattern:
;   static const union { uint32_t l[2]; double d; } __ln10hix = { .l = { 0x5d8d4113, 0xa8acddaa } };
; The bytes are VAX D_float, not IEEE 754.
@__ln10hix = internal constant [2 x i32] [i32 1569538323, i32 -1465066070], align 4

define double @test_no_fold_dfloat_union() {
; CHECK-LABEL: @test_no_fold_dfloat_union(
; The load must NOT be folded to a constant — the bytes are D_float, not IEEE.
; CHECK: load double, ptr @__ln10hix
  %val = load double, ptr @__ln10hix, align 4
  ret double %val
}

; Verify that loading an integer from the same data IS still folded (unaffected).
define i64 @test_int_fold_still_works() {
; CHECK-LABEL: @test_int_fold_still_works(
; CHECK: ret i64
  %val = load i64, ptr @__ln10hix, align 4
  ret i64 %val
}

; Verify that loading a float from float constant data IS still folded.
; Normal double-from-double loads should still work since they don't go
; through the reinterpret path.
@normal_double = internal constant double 3.140000e+00, align 4

define double @test_normal_double_fold() {
; CHECK-LABEL: @test_normal_double_fold(
; CHECK: ret double 3.140000e+00
  %val = load double, ptr @normal_double, align 4
  ret double %val
}
