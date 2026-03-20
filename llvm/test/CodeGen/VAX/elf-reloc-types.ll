; RUN: llc -mtriple=vax-unknown-netbsdelf -filetype=obj -o %t.o < %s
; RUN: llvm-readelf -r %t.o | FileCheck %s

; Verify that external globals produce R_VAX_GOT32 relocations in the default
; (PIC) relocation model, and external calls produce R_VAX_PLT32.
; This is critical for dynamic linking: R_VAX_PC32 for undefined symbols does
; not trigger COPY relocations in the VAX BFD linker.

target triple = "vax-unknown-netbsdelf"

@extern_data = external global i32
declare void @extern_func()

; CHECK: Relocation section
; CHECK-DAG: R_VAX_GOT32 {{.*}} extern_data
; CHECK-DAG: R_VAX_PLT32 {{.*}} extern_func

define i32 @use_extern_data() {
  %v = load i32, ptr @extern_data
  ret i32 %v
}

define void @call_extern_func() {
  call void @extern_func()
  ret void
}
