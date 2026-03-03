; RUN: llc -mtriple=vax-unknown-netbsdelf -relocation-model=pic < %s | FileCheck %s

; Test PIC code generation. VAX uses PC-relative addressing for all globals,
; which is inherently position-independent for local/DSO-local symbols.

@local_var = internal global i32 0

define i32 @load_local() {
; CHECK-LABEL: load_local:
; CHECK: movl local_var, %r0
  %v = load i32, ptr @local_var
  ret i32 %v
}

define void @store_local(i32 %v) {
; CHECK-LABEL: store_local:
; CHECK: movl {{.*}}, local_var
  store i32 %v, ptr @local_var
  ret void
}

@extern_var = external global i32

define i32 @load_extern() {
; CHECK-LABEL: load_extern:
; CHECK: movl extern_var, %r0
  %v = load i32, ptr @extern_var
  ret i32 %v
}

declare i32 @extern_func()

define i32 @call_extern() {
; CHECK-LABEL: call_extern:
; CHECK: calls $0, extern_func
  %r = call i32 @extern_func()
  ret i32 %r
}
