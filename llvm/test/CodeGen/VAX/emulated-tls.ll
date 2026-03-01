; RUN: llc -march=vax < %s | FileCheck %s

; Test that __thread variables are lowered via emulated TLS (__emutls_*).
; VAX has no hardware TLS register, so all TLS access goes through runtime calls.

@x = thread_local global i32 0, align 4

define i32 @read_tls() nounwind {
; CHECK-LABEL: read_tls:
; CHECK: __emutls_v.x
; CHECK: calls
  %val = load i32, ptr @x, align 4
  ret i32 %val
}

define void @write_tls(i32 %v) nounwind {
; CHECK-LABEL: write_tls:
; CHECK: __emutls_v.x
; CHECK: calls
  store i32 %v, ptr @x, align 4
  ret void
}
