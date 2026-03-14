; RUN: llc -march=vax < %s | FileCheck %s

target datalayout = "e-m:e-p:32:32-i64:32-f64:32-a:0:32-n32"
target triple = "vax-unknown-netbsdelf"

declare void @use_ptr(ptr)
declare void @use_two(ptr, ptr)

; Test: LEA_FI + PUSHL with dead register → PUSHAL
define void @push_local_addr() {
; CHECK-LABEL: push_local_addr:
; CHECK:       pushal -16(%fp)
; CHECK-NEXT:  calls $1, use_ptr
  %buf = alloca [16 x i8]
  call void @use_ptr(ptr %buf)
  ret void
}

; Test: LEA_FI result used after PUSHL → must NOT combine (register stays live)
define void @push_local_addr_reused() {
; CHECK-LABEL: push_local_addr_reused:
; CHECK:       pushal -16(%fp)
; CHECK-NEXT:  calls $1, use_ptr
; CHECK-NEXT:  pushal -16(%fp)
; CHECK-NEXT:  calls $1, use_ptr
  %buf = alloca [16 x i8]
  call void @use_ptr(ptr %buf)
  call void @use_ptr(ptr %buf)
  ret void
}

; Test: Two different local addresses → both can PUSHAL
define void @push_two_addrs() {
; CHECK-LABEL: push_two_addrs:
; CHECK:       pushal -16(%fp)
; CHECK-NEXT:  pushal -32(%fp)
; CHECK-NEXT:  calls $2, use_two
  %a = alloca [16 x i8]
  %b = alloca [16 x i8]
  call void @use_two(ptr %b, ptr %a)
  ret void
}

; Test: standalone LEA_FI (not pushed) → MOVAL
define ptr @return_local_addr() {
; CHECK-LABEL: return_local_addr:
; CHECK:       moval -16(%fp), %r0
; CHECK-NEXT:  ret
  %buf = alloca [16 x i8]
  ret ptr %buf
}

; --- ISel PUSHAL patterns (global/external addresses) ---

@gvar = global i32 0

; Test: pushing a global address → PUSHAL (ISel, not peephole)
define void @push_global_addr() {
; CHECK-LABEL: push_global_addr:
; CHECK:       pushal gvar
; CHECK-NEXT:  calls $1, use_ptr
  call void @use_ptr(ptr @gvar)
  ret void
}

declare void @use_int(i32)

; Test: loading a global value → NOT PUSHAL (value, not address)
define void @push_global_value() {
; CHECK-LABEL: push_global_value:
; CHECK-NOT:   pushal
; CHECK:       pushl gvar
  %v = load i32, ptr @gvar
  call void @use_int(i32 %v)
  ret void
}

; Test: global address used twice → each call gets PUSHAL
define void @push_global_twice() {
; CHECK-LABEL: push_global_twice:
; CHECK:       pushal gvar
; CHECK:       calls $1, use_ptr
; CHECK:       pushal gvar
; CHECK:       calls $1, use_ptr
  call void @use_ptr(ptr @gvar)
  call void @use_ptr(ptr @gvar)
  ret void
}
