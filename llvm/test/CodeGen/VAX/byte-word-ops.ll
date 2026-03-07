; RUN: llc -mtriple=vax-unknown-netbsdelf < %s | FileCheck %s
;
; Phase 33C: Byte/word instruction coverage — compare, logic, complement, clear.
; Tests use memory operands where possible to prevent DAG combiner from
; promoting i8/i16 ops to i32 (which happens when args arrive as longwords).

;--- Byte compare + branch ---

define i32 @cmpb_branch(i8 signext %a, i8 signext %b) {
; CHECK-LABEL: cmpb_branch:
; CHECK:       cmpb
; CHECK:       bneq
entry:
  %cmp = icmp eq i8 %a, %b
  br i1 %cmp, label %if.then, label %if.else
if.then:
  ret i32 1
if.else:
  ret i32 0
}

define i32 @tstb_branch(i8 signext %a) {
; CHECK-LABEL: tstb_branch:
; CHECK:       tstb
entry:
  %cmp = icmp eq i8 %a, 0
  br i1 %cmp, label %if.then, label %if.else
if.then:
  ret i32 1
if.else:
  ret i32 0
}

;--- Word compare + branch ---

define i32 @cmpw_branch(i16 signext %a, i16 signext %b) {
; CHECK-LABEL: cmpw_branch:
; CHECK:       cmpw
; CHECK:       bneq
entry:
  %cmp = icmp eq i16 %a, %b
  br i1 %cmp, label %if.then, label %if.else
if.then:
  ret i32 1
if.else:
  ret i32 0
}

define i32 @tstw_branch(i16 signext %a) {
; CHECK-LABEL: tstw_branch:
; CHECK:       tstw
entry:
  %cmp = icmp eq i16 %a, 0
  br i1 %cmp, label %if.then, label %if.else
if.then:
  ret i32 1
if.else:
  ret i32 0
}

;--- Unsigned byte compare ---

define i32 @cmpb_unsigned(i8 zeroext %a, i8 zeroext %b) {
; CHECK-LABEL: cmpb_unsigned:
; CHECK:       cmpb
entry:
  %cmp = icmp ugt i8 %a, %b
  br i1 %cmp, label %if.then, label %if.else
if.then:
  ret i32 1
if.else:
  ret i32 0
}

;--- Byte OR (memory operands for native bisb3) ---

define signext i8 @or_i8_mem(ptr %p, ptr %q) {
; CHECK-LABEL: or_i8_mem:
; CHECK:       bisb3
; CHECK:       ret
  %a = load i8, ptr %p
  %b = load i8, ptr %q
  %r = or i8 %a, %b
  ret i8 %r
}

;--- Word OR (memory operands for native bisw3) ---

define signext i16 @or_i16_mem(ptr %p, ptr %q) {
; CHECK-LABEL: or_i16_mem:
; CHECK:       bisw3
; CHECK:       ret
  %a = load i16, ptr %p
  %b = load i16, ptr %q
  %r = or i16 %a, %b
  ret i16 %r
}

;--- Byte XOR (memory operands for native xorb3) ---

define signext i8 @xor_i8_mem(ptr %p, ptr %q) {
; CHECK-LABEL: xor_i8_mem:
; CHECK:       xorb3
; CHECK:       ret
  %a = load i8, ptr %p
  %b = load i8, ptr %q
  %r = xor i8 %a, %b
  ret i8 %r
}

;--- Word XOR (memory operands for native xorw3) ---

define signext i16 @xor_i16_mem(ptr %p, ptr %q) {
; CHECK-LABEL: xor_i16_mem:
; CHECK:       xorw3
; CHECK:       ret
  %a = load i16, ptr %p
  %b = load i16, ptr %q
  %r = xor i16 %a, %b
  ret i16 %r
}

;--- Byte AND (memory operands — via bicb3) ---

define signext i8 @and_i8_mem(ptr %p, ptr %q) {
; CHECK-LABEL: and_i8_mem:
; CHECK:       bicb3
; CHECK:       ret
  %a = load i8, ptr %p
  %b = load i8, ptr %q
  %r = and i8 %a, %b
  ret i8 %r
}

;--- Word AND (memory operands — via bicw3) ---

define signext i16 @and_i16_mem(ptr %p, ptr %q) {
; CHECK-LABEL: and_i16_mem:
; CHECK:       bicw3
; CHECK:       ret
  %a = load i16, ptr %p
  %b = load i16, ptr %q
  %r = and i16 %a, %b
  ret i16 %r
}

;--- Byte AND with constant ---

define signext i8 @and_i8_const(ptr %p) {
; CHECK-LABEL: and_i8_const:
; CHECK:       bicb3 $-16
; CHECK:       ret
  %a = load i8, ptr %p
  %r = and i8 %a, 15
  ret i8 %r
}

;--- Byte complement (NOT) ---

define signext i8 @not_i8(i8 signext %a) {
; CHECK-LABEL: not_i8:
; CHECK:       mcomb
; CHECK:       ret
  %r = xor i8 %a, -1
  ret i8 %r
}

;--- Word complement (NOT) ---

define signext i16 @not_i16(i16 signext %a) {
; CHECK-LABEL: not_i16:
; CHECK:       mcomw
; CHECK:       ret
  %r = xor i16 %a, -1
  ret i16 %r
}

;--- Store zero byte ---

define void @store_zero_i8(ptr %p) {
; CHECK-LABEL: store_zero_i8:
; CHECK:       movb $0
; CHECK:       ret
  store i8 0, ptr %p
  ret void
}

;--- Store zero word ---

define void @store_zero_i16(ptr %p) {
; CHECK-LABEL: store_zero_i16:
; CHECK:       movw $0
; CHECK:       ret
  store i16 0, ptr %p
  ret void
}

;--- Store non-zero byte constant ---

define void @store_const_i8(ptr %p) {
; CHECK-LABEL: store_const_i8:
; CHECK:       movb $42
; CHECK:       ret
  store i8 42, ptr %p
  ret void
}

;--- Byte SELECT_CC (cmpb + branch diamond) ---

define signext i8 @select_i8(i8 signext %a, i8 signext %b, i8 signext %x, i8 signext %y) {
; CHECK-LABEL: select_i8:
; CHECK:       cmpb
; CHECK:       beql
; CHECK:       ret
  %cmp = icmp eq i8 %a, %b
  %r = select i1 %cmp, i8 %x, i8 %y
  ret i8 %r
}

;--- Word SELECT_CC (cmpw + branch diamond) ---

define signext i16 @select_i16(i16 signext %a, i16 signext %b, i16 signext %x, i16 signext %y) {
; CHECK-LABEL: select_i16:
; CHECK:       cmpw
; CHECK:       beql
; CHECK:       ret
  %cmp = icmp eq i16 %a, %b
  %r = select i1 %cmp, i16 %x, i16 %y
  ret i16 %r
}

;--- Byte immediate materialization ---

define signext i8 @const_i8() {
; CHECK-LABEL: const_i8:
; CHECK:       movl $7, %r0
; CHECK:       ret
  ret i8 7
}

;--- Direct i8→i16 sign-extend (CVTBW) ---

define void @sext_b_to_w(ptr %p, ptr %q) {
; CHECK-LABEL: sext_b_to_w:
; CHECK:       cvtbw
; CHECK:       ret
  %v = load i8, ptr %p
  %ext = sext i8 %v to i16
  store i16 %ext, ptr %q
  ret void
}

;--- Direct i8→i16 zero-extend (MOVZBW) ---

define void @zext_b_to_w(ptr %p, ptr %q) {
; CHECK-LABEL: zext_b_to_w:
; CHECK:       movzbw
; CHECK:       ret
  %v = load i8, ptr %p
  %ext = zext i8 %v to i16
  store i16 %ext, ptr %q
  ret void
}
