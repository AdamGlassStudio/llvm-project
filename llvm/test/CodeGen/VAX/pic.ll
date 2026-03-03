; RUN: llc -mtriple=vax-unknown-netbsdelf -relocation-model=pic < %s | FileCheck %s

; Test PIC code generation. VAX uses PC-relative addressing for all globals.
; Local/DSO-local symbols use direct PC-relative (R_VAX_PC32).
; External symbols use GOT (R_VAX_GOT32) for data and PLT (R_VAX_PLT32) for calls.

@local_var = internal global i32 0

define i32 @load_local() {
; CHECK-LABEL: load_local:
; Local: direct PC-relative, no GOT
; CHECK: movl local_var, %r0
; CHECK-NOT: @GOT
  %v = load i32, ptr @local_var
  ret i32 %v
}

define void @store_local(i32 %v) {
; CHECK-LABEL: store_local:
; CHECK: movl {{.*}}, local_var
; CHECK-NOT: @GOT
  store i32 %v, ptr @local_var
  ret void
}

@extern_var = external global i32

define i32 @load_extern() {
; CHECK-LABEL: load_extern:
; External: GOT-indirect load
; CHECK: movl extern_var@GOT, %r0
; CHECK: movl (%r0), %r0
  %v = load i32, ptr @extern_var
  ret i32 %v
}

declare i32 @extern_func()

define i32 @call_extern() {
; CHECK-LABEL: call_extern:
; External call: through PLT
; CHECK: calls $0, extern_func@PLT
  %r = call i32 @extern_func()
  ret i32 %r
}
