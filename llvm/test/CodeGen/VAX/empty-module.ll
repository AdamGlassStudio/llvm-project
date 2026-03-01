; Phase 2 smoke test: verify llc accepts a vax-unknown-netbsdelf triple and
; processes an empty module (no functions → no ISel) without crashing.
;
; RUN: llc -mtriple=vax-unknown-netbsdelf -o /dev/null %s
; RUN: llc -mtriple=vax-unknown-netbsdelf -o - %s | FileCheck %s

; CHECK-NOT: error
; CHECK-NOT: UNREACHABLE

target datalayout = "e-m:e-p:32:32-i1:8:32-i8:8:32-i16:16:32-i64:32-f64:32-a:0:32-n32"
target triple = "vax-unknown-netbsdelf"
