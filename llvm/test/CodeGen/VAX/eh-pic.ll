; RUN: llc -mtriple=vax-unknown-netbsdelf -relocation-model=pic < %s | FileCheck %s

; Test DWARF CFI exception handling in PIC mode.  PIC mode uses indirect
; pcrel encoding for personality and LSDA references, producing the correct
; R_VAX_GOT32 relocations for shared library support.

@_ZTIi = external constant ptr

declare i32 @__gxx_personality_v0(...)
declare ptr @__cxa_begin_catch(ptr)
declare void @__cxa_end_catch()
declare void @may_throw()

; CHECK-LABEL: try_catch_pic:
; CHECK: .cfi_startproc
; PIC personality encoding: indirect|pcrel|sdata4 = 0x9b = 155
; CHECK: .cfi_personality 155,
; CHECK: .cfi_lsda 27,
; CHECK: .cfi_def_cfa %fp, 0
; CHECK: .cfi_offset %pc, 16
; CHECK: .cfi_offset %fp, 12
; CHECK: .cfi_offset %ap, 8
; CHECK: calls $0, may_throw_ret@PLT
define i32 @try_catch_pic() personality ptr @__gxx_personality_v0 {
entry:
  %r = invoke i32 @may_throw_ret()
          to label %cont unwind label %lpad
cont:
  ret i32 %r
lpad:
  %lp = landingpad { ptr, i32 }
          catch ptr @_ZTIi
  %exc = extractvalue { ptr, i32 } %lp, 0
  %sel = extractvalue { ptr, i32 } %lp, 1
  %p = call ptr @__cxa_begin_catch(ptr %exc)
  call void @__cxa_end_catch()
  ret i32 0
}

declare i32 @may_throw_ret()
