; RUN: llc -mtriple=vax-unknown-netbsdelf < %s | FileCheck %s
;
; Phase 33A: Sub-register infrastructure validation.
; Verify that adding byte/word sub-register definitions doesn't change
; existing codegen behavior. All i8/i16 operations are still promoted to i32.
;
; Also verifies that function argument passing still promotes i8/i16 to
; longword (required by VAX CALLS convention — all args pushed as longwords).

; --- Argument passing: i8/i16 args arrive as longwords via CALLS ---

define signext i8 @return_i8(i8 signext %a) {
; CHECK-LABEL: return_i8:
; CHECK:       movl 4(%ap), %r0
; CHECK-NEXT:  ret
  ret i8 %a
}

define signext i16 @return_i16(i16 signext %a) {
; CHECK-LABEL: return_i16:
; CHECK:       movl 4(%ap), %r0
; CHECK-NEXT:  ret
  ret i16 %a
}

define zeroext i8 @return_i8_zext(i8 zeroext %a) {
; CHECK-LABEL: return_i8_zext:
; CHECK:       movl 4(%ap), %r0
; CHECK-NEXT:  ret
  ret i8 %a
}

; Outgoing i8 arg is pushed as a longword
define i32 @call_with_i8(i8 signext %a) {
; CHECK-LABEL: call_with_i8:
; CHECK:       movl 4(%ap), %r0
; CHECK:       pushl %r0
; CHECK-NEXT:  calls $1, return_i8
; CHECK-NEXT:  ret
  %r = call signext i8 @return_i8(i8 signext %a)
  %ext = sext i8 %r to i32
  ret i32 %ext
}

; --- i8 arithmetic: currently promoted to i32 ---

define signext i8 @add_i8(i8 signext %a, i8 signext %b) {
; CHECK-LABEL: add_i8:
; CHECK:       movl 8(%ap), %r0
; CHECK-NEXT:  movl 4(%ap), %r1
; CHECK-NEXT:  addl2 %r1, %r0
; CHECK-NEXT:  cvtbl %r0, %r0
; CHECK-NEXT:  ret
  %sum = add i8 %a, %b
  ret i8 %sum
}

define signext i16 @add_i16(i16 signext %a, i16 signext %b) {
; CHECK-LABEL: add_i16:
; CHECK:       movl 8(%ap), %r0
; CHECK-NEXT:  movl 4(%ap), %r1
; CHECK-NEXT:  addl2 %r1, %r0
; CHECK-NEXT:  cvtwl %r0, %r0
; CHECK-NEXT:  ret
  %sum = add i16 %a, %b
  ret i16 %sum
}

; --- i8/i16 loads and stores (truncating/extending) ---

define signext i8 @load_i8(ptr %p) {
; CHECK-LABEL: load_i8:
; CHECK:       cvtbl {{.*}}, %r0
; CHECK:       ret
  %v = load i8, ptr %p
  ret i8 %v
}

define void @store_i8(ptr %p, i8 signext %v) {
; CHECK-LABEL: store_i8:
; CHECK:       movb {{.*}}
; CHECK:       ret
  store i8 %v, ptr %p
  ret void
}

define signext i16 @load_i16(ptr %p) {
; CHECK-LABEL: load_i16:
; CHECK:       cvtwl {{.*}}, %r0
; CHECK:       ret
  %v = load i16, ptr %p
  ret i16 %v
}

define void @store_i16(ptr %p, i16 signext %v) {
; CHECK-LABEL: store_i16:
; CHECK:       movw {{.*}}
; CHECK:       ret
  store i16 %v, ptr %p
  ret void
}

; --- Zero-extend loads ---

define i32 @zextload_i8(ptr %p) {
; CHECK-LABEL: zextload_i8:
; CHECK:       movzbl {{.*}}, %r0
; CHECK:       ret
  %v = load i8, ptr %p
  %ext = zext i8 %v to i32
  ret i32 %ext
}

define i32 @zextload_i16(ptr %p) {
; CHECK-LABEL: zextload_i16:
; CHECK:       movzwl {{.*}}, %r0
; CHECK:       ret
  %v = load i16, ptr %p
  %ext = zext i16 %v to i32
  ret i32 %ext
}

; --- Two i8 args (verify second arg at offset 8) ---

define signext i8 @second_i8_arg(i8 signext %a, i8 signext %b) {
; CHECK-LABEL: second_i8_arg:
; CHECK:       movl 8(%ap), %r0
; CHECK-NEXT:  ret
  ret i8 %b
}

; --- Mixed i8/i32/i16 args ---

define i32 @mixed_args(i8 signext %a, i32 %b, i16 signext %c) {
; CHECK-LABEL: mixed_args:
; CHECK:       movl 8(%ap), %r0
; CHECK-NEXT:  ret
  ret i32 %b
}
