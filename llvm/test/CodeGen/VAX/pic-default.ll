; RUN: llc -mtriple=vax-unknown-netbsdelf < %s | FileCheck %s
; RUN: llc -mtriple=vax-unknown-netbsdelf -relocation-model=pic < %s | FileCheck %s --check-prefix=PIC
; RUN: llc -mtriple=vax-unknown-netbsdelf -relocation-model=static < %s | FileCheck %s --check-prefix=STATIC

; Test that the default relocation model (PIC) routes external symbols through
; GOT and external calls through PLT, matching GCC's forced -fPIC behavior.
; This prevents SIGSEGV in dynamically linked binaries where the linker cannot
; create COPY relocations for R_VAX_PC32 references to shared library symbols.

; --- External data: must use GOT in default and PIC modes ---

@extern_var = external global i32
@extern_array = external global [88 x i8]

define i32 @load_extern_var() {
; Default (PIC): GOT-relative
; CHECK-LABEL: load_extern_var:
; CHECK: extern_var@GOT
; PIC-LABEL: load_extern_var:
; PIC: extern_var@GOT
; STATIC-LABEL: load_extern_var:
; STATIC: movl extern_var, %r0
; STATIC-NOT: @GOT
  %v = load i32, ptr @extern_var
  ret i32 %v
}

define void @store_extern_var(i32 %v) {
; CHECK-LABEL: store_extern_var:
; CHECK: extern_var@GOT
; PIC-LABEL: store_extern_var:
; PIC: extern_var@GOT
; STATIC-LABEL: store_extern_var:
; STATIC-NOT: @GOT
  store i32 %v, ptr @extern_var
  ret void
}

define ptr @addr_extern_array() {
; This is the __sF pattern: taking the address of an external array.
; CHECK-LABEL: addr_extern_array:
; CHECK: extern_array@GOT
; STATIC-LABEL: addr_extern_array:
; STATIC: moval extern_array
; STATIC-NOT: @GOT
  ret ptr @extern_array
}

; --- Local data: always uses direct PC-relative ---

@local_var = dso_local global i32 0

define i32 @load_local_var() {
; CHECK-LABEL: load_local_var:
; CHECK: movl local_var, %r0
; CHECK-NOT: @GOT
; PIC-LABEL: load_local_var:
; PIC: movl local_var, %r0
; PIC-NOT: @GOT
; STATIC-LABEL: load_local_var:
; STATIC: movl local_var, %r0
  %v = load i32, ptr @local_var
  ret i32 %v
}

@internal_var = internal global i32 42

define i32 @load_internal_var() {
; CHECK-LABEL: load_internal_var:
; CHECK: movl internal_var, %r0
; CHECK-NOT: @GOT
  %v = load i32, ptr @internal_var
  ret i32 %v
}

; --- External calls: must use PLT in default and PIC modes ---

declare i32 @extern_func()

define i32 @call_extern_func() {
; CHECK-LABEL: call_extern_func:
; CHECK: calls $0, extern_func@PLT
; PIC-LABEL: call_extern_func:
; PIC: calls $0, extern_func@PLT
; STATIC-LABEL: call_extern_func:
; STATIC: calls $0, extern_func
; STATIC-NOT: @PLT
  %r = call i32 @extern_func()
  ret i32 %r
}

; --- Local calls: always direct ---

define internal i32 @local_func() {
  ret i32 99
}

define i32 @call_local_func() {
; CHECK-LABEL: call_local_func:
; CHECK: calls $0, local_func
; CHECK-NOT: @PLT
  %r = call i32 @local_func()
  ret i32 %r
}

; --- Weak symbols: must use GOT (NULL-check pattern) ---

@weak_var = extern_weak global i32

define i1 @check_weak_null() {
; Weak symbols must go through GOT so NULL checks work correctly.
; CHECK-LABEL: check_weak_null:
; CHECK: weak_var@GOT
  %addr = ptrtoint ptr @weak_var to i32
  %isnull = icmp eq i32 %addr, 0
  ret i1 %isnull
}
