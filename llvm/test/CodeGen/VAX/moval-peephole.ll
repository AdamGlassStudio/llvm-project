; RUN: llc -march=vax < %s -o - | FileCheck %s

; Test ADDL3_ri + PUSHL → PUSHAL conversion.
; Two uses of %p force it into a callee-saved register, producing ADDL3_ri
; which the peephole converts to PUSHAL.
define void @push_two_members(ptr %p) {
; CHECK-LABEL: push_two_members:
; CHECK:       pushal 4(%r
; CHECK:       pushal 8(%r
  %b = getelementptr inbounds i8, ptr %p, i32 4
  tail call void @use_ptr(ptr %b)
  %c = getelementptr inbounds i8, ptr %p, i32 8
  tail call void @use_ptr(ptr %c)
  ret void
}

; Test ADDL3_ri → MOVAL conversion for large offsets (> 63).
; Three uses force %p into callee-saved register. The return value
; (base + 256) becomes ADDL3_ri which the peephole converts to MOVAL.
define ptr @get_large_offset(ptr %p) {
; CHECK-LABEL: get_large_offset:
; CHECK:       moval 256(%r
  %a = getelementptr inbounds i8, ptr %p, i32 0
  tail call void @use_ptr(ptr %a)
  %b = getelementptr inbounds i8, ptr %p, i32 4
  tail call void @use_ptr(ptr %b)
  %member = getelementptr inbounds i8, ptr %p, i32 256
  ret ptr %member
}

; Negative offset should also use MOVAL (always outside 0-63 range).
define ptr @get_negative_offset(ptr %p) {
; CHECK-LABEL: get_negative_offset:
; CHECK:       moval -8(%r
  %a = getelementptr inbounds i8, ptr %p, i32 0
  tail call void @use_ptr(ptr %a)
  %b = getelementptr inbounds i8, ptr %p, i32 4
  tail call void @use_ptr(ptr %b)
  %member = getelementptr inbounds i8, ptr %p, i32 -8
  ret ptr %member
}

declare void @use_ptr(ptr)
