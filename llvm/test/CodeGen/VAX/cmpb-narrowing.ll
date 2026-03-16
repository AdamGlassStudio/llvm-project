; RUN: llc -mtriple=vax-unknown-netbsdelf -O2 < %s | FileCheck %s
;
; Test that byte comparisons naturally use CMPB/TSTB instructions
; with legal i8 type.

; Basic equality comparison against constant: uses cmpb.
define i32 @cmpb_eq_42(ptr %p) {
; CHECK-LABEL: cmpb_eq_42:
; CHECK:       cmpb %r{{[0-9]+}}, %r{{[0-9]+}}
; CHECK-NEXT:  bneq
  %v = load i8, ptr %p
  %zext = zext i8 %v to i32
  %cmp = icmp eq i32 %zext, 42
  br i1 %cmp, label %then, label %else
then:
  ret i32 1
else:
  ret i32 0
}

; Comparison against zero: movb sets PSW, so beql works directly.
define i32 @tstb_zero(ptr %p) {
; CHECK-LABEL: tstb_zero:
; CHECK:       movb (%r{{[0-9]+}}), %r{{[0-9]+}}
; CHECK-NEXT:  beql
  %v = load i8, ptr %p
  %zext = zext i8 %v to i32
  %cmp = icmp eq i32 %zext, 0
  br i1 %cmp, label %then, label %else
then:
  ret i32 1
else:
  ret i32 0
}

; Unsigned less-than: should use cmpb with unsigned branch.
define i32 @cmpb_ult(ptr %p) {
; CHECK-LABEL: cmpb_ult:
; CHECK:       cmpb %r{{[0-9]+}}, %r{{[0-9]+}}
; CHECK-NEXT:  bgtru
  %v = load i8, ptr %p
  %zext = zext i8 %v to i32
  %cmp = icmp ult i32 %zext, 10
  br i1 %cmp, label %then, label %else
then:
  ret i32 1
else:
  ret i32 0
}

; Comparison against max byte value (255 = -1 in i8).
define i32 @cmpb_eq_255(ptr %p) {
; CHECK-LABEL: cmpb_eq_255:
; CHECK:       cmpb %r{{[0-9]+}}, %r{{[0-9]+}}
; CHECK-NEXT:  bneq
  %v = load i8, ptr %p
  %zext = zext i8 %v to i32
  %cmp = icmp eq i32 %zext, 255
  br i1 %cmp, label %then, label %else
then:
  ret i32 1
else:
  ret i32 0
}

; Constant >= 256: must use movzbl+cmpl (value doesn't fit in byte).
define i32 @no_narrow_big_const(ptr %p) {
; CHECK-LABEL: no_narrow_big_const:
; CHECK:       movzbl (%r{{[0-9]+}}), %r{{[0-9]+}}
; CHECK:       cmpl %r{{[0-9]+}}, $300
  %v = load i8, ptr %p
  %zext = zext i8 %v to i32
  %cmp = icmp eq i32 %zext, 300
  br i1 %cmp, label %then, label %else
then:
  ret i32 1
else:
  ret i32 0
}

; Multi-use of zero-extended value: cmpb for comparison, movzbl for return.
define i32 @no_narrow_multi_use(ptr %p) {
; CHECK-LABEL: no_narrow_multi_use:
; CHECK:       cmpb %r{{[0-9]+}}, %r{{[0-9]+}}
; CHECK:       movzbl %r{{[0-9]+}}, %r0
  %v = load i8, ptr %p
  %zext = zext i8 %v to i32
  %cmp = icmp eq i32 %zext, 42
  br i1 %cmp, label %then, label %else
then:
  ret i32 %zext
else:
  ret i32 0
}

; Frame-index addressing: compare byte on stack.
define i32 @cmpb_stack(i8 %val) {
; CHECK-LABEL: cmpb_stack:
; CHECK:       cmpb
  %slot = alloca i8
  store i8 %val, ptr %slot
  %v = load i8, ptr %slot
  %zext = zext i8 %v to i32
  %cmp = icmp eq i32 %zext, 7
  br i1 %cmp, label %then, label %else
then:
  ret i32 1
else:
  ret i32 0
}

; Signed comparison with zext'd value: must use movzbl+cmpl.
define i32 @no_narrow_signed(ptr %p) {
; CHECK-LABEL: no_narrow_signed:
; CHECK:       movzbl (%r{{[0-9]+}}), %r{{[0-9]+}}
; CHECK:       cmpl %r{{[0-9]+}}, $101
  %v = load i8, ptr %p
  %zext = zext i8 %v to i32
  %cmp = icmp sgt i32 %zext, 100
  br i1 %cmp, label %then, label %else
then:
  ret i32 1
else:
  ret i32 0
}
