; RUN: llc -mtriple=vax-unknown-netbsdelf < %s | FileCheck %s
;
; Test that the peephole converts large-immediate MOVL/PUSHL to
; shorter instruction forms (ASHL for base<<shift, MNEGL for small negatives).

; --- MOVL_ri → ASHL_ii (base << shift, base in [1,63]) ---

define i32 @ashl_power_of_2() {
; CHECK-LABEL: ashl_power_of_2:
; CHECK: ashl $11, $1, %r0
  ret i32 2048
}

define i32 @ashl_large_power() {
; CHECK-LABEL: ashl_large_power:
; CHECK: ashl $20, $1, %r0
  ret i32 1048576
}

define i32 @ashl_factored() {
; CHECK-LABEL: ashl_factored:
; CHECK: ashl $5, $13, %r0
  ret i32 416
}

define i32 @ashl_sign_bit() {
; CHECK-LABEL: ashl_sign_bit:
; CHECK: ashl $31, $1, %r0
  ret i32 -2147483648
}

; --- MOVL_ri → MNEGL_i (small negative, abs in [1,63]) ---

define i32 @mnegl_minus1() {
; CHECK-LABEL: mnegl_minus1:
; CHECK: mnegl $1, %r0
  ret i32 -1
}

define i32 @mnegl_minus63() {
; CHECK-LABEL: mnegl_minus63:
; CHECK: mnegl $63, %r0
  ret i32 -63
}

define i32 @mnegl_minus2() {
; CHECK-LABEL: mnegl_minus2:
; CHECK: mnegl $2, %r0
  ret i32 -2
}

; --- Should NOT be converted (literal mode already short, or no pattern) ---

define i32 @no_convert_small() {
; CHECK-LABEL: no_convert_small:
; CHECK: movl $42, %r0
  ret i32 42
}

define i32 @no_convert_63() {
; CHECK-LABEL: no_convert_63:
; CHECK: movl $63, %r0
  ret i32 63
}

define i32 @no_convert_odd() {
; CHECK-LABEL: no_convert_odd:
; CHECK: movl $255, %r0
  ret i32 255
}

define i32 @no_convert_neg64() {
; CHECK-LABEL: no_convert_neg64:
; CHECK-NOT: mnegl
  ret i32 -64
}

; --- PUSHL_i → ASHL_iip / MNEGL_ip (via call arguments) ---

declare void @sink(i32, i32)

define void @push_ashl() {
; CHECK-LABEL: push_ashl:
; CHECK:       pushl $0
; CHECK-NEXT:  ashl $11, $1, -(%sp)
  call void @sink(i32 2048, i32 0)
  ret void
}

define void @push_mnegl() {
; CHECK-LABEL: push_mnegl:
; CHECK:       pushl $0
; CHECK-NEXT:  mnegl $1, -(%sp)
  call void @sink(i32 -1, i32 0)
  ret void
}
