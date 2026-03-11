; RUN: llc -mtriple=vax-unknown-netbsdelf < %s | FileCheck %s

; Test that i64 ordered comparisons use efficient multi-block branch
; sequences (via SELECT_CC_I64 combine) instead of the generic 3-setcc
; expansion which materializes booleans in registers.

; Signed less-than: should use cmpl+beql+blss (not 3 setcc chains)
define i32 @cmp_slt_i64(i64 %a, i64 %b, i32 %t, i32 %f) {
; CHECK-LABEL: cmp_slt_i64:
; CHECK: cmpl
; CHECK: beql
; CHECK: cmpl
; CHECK-NOT: xorl
; CHECK: ret
entry:
  %cmp = icmp slt i64 %a, %b
  %r = select i1 %cmp, i32 %t, i32 %f
  ret i32 %r
}

; Unsigned greater-or-equal
define i32 @cmp_uge_i64(i64 %a, i64 %b, i32 %t, i32 %f) {
; CHECK-LABEL: cmp_uge_i64:
; CHECK: cmpl
; CHECK: beql
; CHECK: cmpl
; CHECK-NOT: xorl
; CHECK: ret
entry:
  %cmp = icmp uge i64 %a, %b
  %r = select i1 %cmp, i32 %t, i32 %f
  ret i32 %r
}

; Real-world pattern from sblksize (boot chain)
define i32 @sblksize(i32 %bsize, i64 %size, i32 %lbn, i32 %bshift) {
; CHECK-LABEL: sblksize:
; CHECK: cmpl
; CHECK: beql
; CHECK: cmpl
; CHECK: ret
entry:
  %add = add nsw i32 %lbn, 1
  %conv = sext i32 %add to i64
  %sh_prom = zext nneg i32 %bshift to i64
  %shl = shl i64 %conv, %sh_prom
  %cmp.not = icmp slt i64 %size, %shl
  %0 = trunc i64 %size to i32
  %conv2 = and i32 %0, 65535
  %retval.0 = select i1 %cmp.not, i32 %conv2, i32 %bsize
  ret i32 %retval.0
}
