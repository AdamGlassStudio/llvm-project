; RUN: llc -mtriple=vax-unknown-netbsdelf -relocation-model=pic < %s | FileCheck %s
; RUN: llc -mtriple=vax-unknown-netbsdelf -relocation-model=static < %s | FileCheck %s --check-prefix=STATIC

; Test PIC code generation.  In PIC mode, external/interposable symbols use
; GOT-relative addressing for data and PLT-relative calls for functions.
; DSO-local symbols use direct PC-relative addressing.

@local_var = internal global i32 0

define i32 @load_local() {
; CHECK-LABEL: load_local:
; Local: direct PC-relative (no GOT needed)
; CHECK: movl local_var, %r0
; CHECK-NOT: @GOT
; STATIC-LABEL: load_local:
; STATIC: movl local_var, %r0
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
; External in PIC: GOT-relative
; CHECK: movl extern_var@GOT, %r0
; STATIC-LABEL: load_extern:
; STATIC: movl extern_var, %r0
; STATIC-NOT: @GOT
  %v = load i32, ptr @extern_var
  ret i32 %v
}

declare i32 @extern_func()

define i32 @call_extern() {
; CHECK-LABEL: call_extern:
; External call in PIC: PLT-relative
; CHECK: calls $0, extern_func@PLT
; STATIC-LABEL: call_extern:
; STATIC: calls $0, extern_func
; STATIC-NOT: @PLT
  %r = call i32 @extern_func()
  ret i32 %r
}
