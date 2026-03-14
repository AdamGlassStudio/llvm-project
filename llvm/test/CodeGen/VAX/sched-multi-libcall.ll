; RUN: llc -march=vax < %s -o /dev/null
; Regression test: multiple legalized libcalls in the same BB must not crash
; the pre-RA list scheduler. The legalizer expands fsqrt and fp_to_uint i64
; into libcalls chaining from EntryToken, creating parallel call sequences
; whose results merge via TokenFactors. FindCallSeqStart must handle nested
; TokenFactors where some branches have no call sequence.

declare void @use(i32, i32)

define void @multi_libcall(double %x) {
entry:
  ; fsqrt expands to a sqrt libcall
  %sqrt = call double @llvm.sqrt.f64(double %x)
  %mul = fmul double %sqrt, 65536.0
  %conv1 = fptoui double %mul to i32

  ; fabs + fldexp + fp_to_uint i64 expands to ldexp + __fixunsdfdi libcalls
  %abs = call double @llvm.fabs.f64(double %x)
  %scaled = call double @llvm.ldexp.f64.i32(double %abs, i32 32)
  %wide = fptoui double %scaled to i64
  %lo = trunc i64 %wide to i32
  %hi64 = lshr i64 %wide, 32
  %hi = trunc i64 %hi64 to i32

  call void @use(i32 %conv1, i32 %lo)
  call void @use(i32 %hi, i32 0)
  ret void
}

declare double @llvm.sqrt.f64(double)
declare double @llvm.fabs.f64(double)
declare double @llvm.ldexp.f64.i32(double, i32)
