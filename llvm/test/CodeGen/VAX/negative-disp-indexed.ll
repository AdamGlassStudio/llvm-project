; RUN: llc -mtriple=vax-unknown-netbsdelf -O1 < %s -o /dev/null
; RUN: llc -mtriple=vax-unknown-netbsdelf -O2 < %s -o /dev/null

; Regression test: negative displacements in indexed addressing caused an
; APInt assertion failure.  getTargetConstant(getSExtValue(), MVT::i32) passes
; a sign-extended 64-bit value (e.g. -4 = 0xFFFFFFFFFFFFFFFC) to APInt(32, val)
; which asserts val <= UINT32_MAX.  Fix: use APInt(32, val, isSigned=true).
;
; Reduced from zstd's divsufsort.c ss_mintrosort function.

target datalayout = "e-m:e-p:32:32-i1:8:32-i8:8:32-i16:16:32-i64:32-f64:32-a:0:32-n32"

define fastcc ptr @negative_disp_indexed(ptr %last, i1 %cond, i32 %idx) {
entry:
  %add.ptr = getelementptr i8, ptr %last, i32 -4
  %scaled = getelementptr [4 x i8], ptr %add.ptr, i32 %idx
  %val = load i32, ptr %scaled, align 4
  %elem = getelementptr i8, ptr null, i32 %val
  %result = select i1 %cond, ptr %elem, ptr %add.ptr
  ret ptr %result
}
