; RUN: llc -march=vax -filetype=obj -o %t.o %s
; RUN: llvm-readelf -h %t.o | FileCheck %s --check-prefix=ELF-HEADER
; RUN: llvm-readelf -r %t.o | FileCheck %s --check-prefix=RELOCS

; Verify that -filetype=obj produces valid VAX ELF objects.

; ELF-HEADER: Machine: Digital VAX

; Check that an external global reference produces a relocation.
; RELOCS: gvar

target triple = "vax-unknown-netbsdelf"

@gvar = external global i32

define i32 @ret_zero() {
  ret i32 0
}

define i32 @add(i32 %a, i32 %b) {
  %r = add i32 %a, %b
  ret i32 %r
}

define i32 @load_global() {
  %v = load i32, i32* @gvar
  ret i32 %v
}

define i32 @branch(i32 %a, i32 %b) {
  %cmp = icmp sgt i32 %a, %b
  br i1 %cmp, label %then, label %else
then:
  ret i32 %a
else:
  ret i32 %b
}
