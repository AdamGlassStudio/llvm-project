; RUN: llc -march=vax -relocation-model=static < %s | FileCheck %s

; Test DWARF CFI exception handling infrastructure.

@_ZTIi = external constant ptr

declare i32 @__gxx_personality_v0(...)
declare ptr @__cxa_begin_catch(ptr)
declare void @__cxa_end_catch()
declare void @may_throw()

; CHECK-LABEL: try_catch:
; CHECK: .cfi_startproc
; CHECK: .cfi_personality 0, __gxx_personality_v0
; CHECK: .cfi_lsda 0,
; CHECK: .cfi_def_cfa %fp, 0
; CHECK: .cfi_offset %pc, 16
; CHECK: .cfi_offset %fp, 12
; CHECK: .cfi_offset %ap, 8
; CHECK: calls $0, may_throw
; CHECK: .cfi_endproc
define i32 @try_catch() personality ptr @__gxx_personality_v0 {
entry:
  invoke void @may_throw()
    to label %try.cont unwind label %lpad

lpad:
  %0 = landingpad { ptr, i32 }
    catch ptr @_ZTIi
  %1 = extractvalue { ptr, i32 } %0, 0
  %2 = call ptr @__cxa_begin_catch(ptr %1)
  %3 = load i32, ptr %2
  call void @__cxa_end_catch()
  ret i32 %3

try.cont:
  ret i32 0
}

; Test FRAMEADDR intrinsic: should return FP.
; CHECK-LABEL: get_frameaddr:
; CHECK: movl %fp, %r0
declare ptr @llvm.frameaddress(i32)
define ptr @get_frameaddr() {
  %r = call ptr @llvm.frameaddress(i32 0)
  ret ptr %r
}

; Test RETURNADDR intrinsic: should load from FP+16.
; CHECK-LABEL: get_returnaddr:
; CHECK: movl 16(%fp), %r0
declare ptr @llvm.returnaddress(i32)
define ptr @get_returnaddr() {
  %r = call ptr @llvm.returnaddress(i32 0)
  ret ptr %r
}
