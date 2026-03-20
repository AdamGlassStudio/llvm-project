; RUN: llc -march=vax < %s | FileCheck %s

; Test that __thread variables are lowered via emulated TLS (__emutls_*).
; VAX has no hardware TLS register, so all TLS access goes through runtime calls.

@x = thread_local global i32 0, align 4
@y = thread_local global i32 42, align 4
@z = thread_local(initialexec) global i32 0, align 4

define i32 @read_tls() nounwind {
; CHECK-LABEL: read_tls:
; CHECK: __emutls_v.x
; CHECK: calls {{.*}} __emutls_get_address
  %val = load i32, ptr @x, align 4
  ret i32 %val
}

define void @write_tls(i32 %v) nounwind {
; CHECK-LABEL: write_tls:
; CHECK: __emutls_v.x
; CHECK: calls {{.*}} __emutls_get_address
  store i32 %v, ptr @x, align 4
  ret void
}

; Initialized TLS variable — should generate __emutls_t.y template symbol.
define i32 @read_init_tls() nounwind {
; CHECK-LABEL: read_init_tls:
; CHECK: __emutls_v.y
; CHECK: calls {{.*}} __emutls_get_address
  %val = load i32, ptr @y, align 4
  ret i32 %val
}

; initialexec model — still goes through emulation on VAX.
define i32 @read_ie_tls() nounwind {
; CHECK-LABEL: read_ie_tls:
; CHECK: __emutls_v.z
; CHECK: calls {{.*}} __emutls_get_address
  %val = load i32, ptr @z, align 4
  ret i32 %val
}

; CHECK-LABEL: __emutls_t.y:
; CHECK:       .long 42
