; RUN: llc -mtriple=vax-unknown-netbsdelf -O2 < %s | FileCheck %s
;
; Test that comparisons of zero-extended byte loads against small constants
; are narrowed to CMPB/TSTB instructions, eliminating the MOVZBL + CMPL pair.

; Basic equality comparison: should use cmpb, not movzbl+cmpl.
define i32 @cmpb_eq_42(ptr %p) {
; CHECK-LABEL: cmpb_eq_42:
; CHECK:       cmpb (%r0), $42
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

; Comparison against zero: should use tstb.
define i32 @tstb_zero(ptr %p) {
; CHECK-LABEL: tstb_zero:
; CHECK:       tstb (%r0)
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

; Unsigned less-than: should narrow to cmpb.
define i32 @cmpb_ult(ptr %p) {
; CHECK-LABEL: cmpb_ult:
; CHECK:       cmpb (%r0), $9
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

; Comparison against max byte value (255): should still narrow.
define i32 @cmpb_eq_255(ptr %p) {
; CHECK-LABEL: cmpb_eq_255:
; CHECK:       cmpb (%r0), $255
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

; Constant >= 256: should NOT narrow (keep movzbl+cmpl).
define i32 @no_narrow_big_const(ptr %p) {
; CHECK-LABEL: no_narrow_big_const:
; CHECK:       movzbl (%r0), %r0
; CHECK:       cmpl %r0, $300
  %v = load i8, ptr %p
  %zext = zext i8 %v to i32
  %cmp = icmp eq i32 %zext, 300
  br i1 %cmp, label %then, label %else
then:
  ret i32 1
else:
  ret i32 0
}

; Multi-use of zero-extended value: should NOT narrow (load still needed).
define i32 @no_narrow_multi_use(ptr %p) {
; CHECK-LABEL: no_narrow_multi_use:
; CHECK:       movzbl (%r0), %r0
; CHECK:       cmpl %r0, $42
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

; Signed comparison: should NOT narrow (cmpb treats bytes as signed,
; but zext'd value is unsigned — byte 200 = i32 +200, not -56).
define i32 @no_narrow_signed(ptr %p) {
; CHECK-LABEL: no_narrow_signed:
; CHECK:       movzbl (%r0), %r0
; CHECK:       cmpl %r0, $101
  %v = load i8, ptr %p
  %zext = zext i8 %v to i32
  %cmp = icmp sgt i32 %zext, 100
  br i1 %cmp, label %then, label %else
then:
  ret i32 1
else:
  ret i32 0
}
