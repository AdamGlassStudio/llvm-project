; RUN: llc -mtriple=vax-unknown-netbsdelf -relocation-model=pic < %s | FileCheck %s

; Test PIC code generation. VAX uses PC-relative addressing for all globals.
; VAX GAS does not support @GOT or @PLT relocations, so even in PIC mode
; all symbols use direct PC-relative addressing (R_VAX_PC32). True shared
; library support would require a different mechanism.

@local_var = internal global i32 0

define i32 @load_local() {
; CHECK-LABEL: load_local:
; Local: direct PC-relative
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
; External: also direct PC-relative (no GOT on VAX)
; CHECK: movl extern_var, %r0
; CHECK-NOT: @GOT
  %v = load i32, ptr @extern_var
  ret i32 %v
}

declare i32 @extern_func()

define i32 @call_extern() {
; CHECK-LABEL: call_extern:
; External call: direct (no PLT on VAX)
; CHECK: calls $0, extern_func
; CHECK-NOT: @PLT
  %r = call i32 @extern_func()
  ret i32 %r
}
