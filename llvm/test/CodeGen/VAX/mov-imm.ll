; RUN: llc -mtriple=vax-unknown-netbsdelf < %s | FileCheck %s

; CHECK-LABEL: ret42:
; CHECK: movl $42, %r0
; CHECK: ret
define i32 @ret42() {
  ret i32 42
}

; CHECK-LABEL: ret0:
; CHECK: clrl %r0
; CHECK: ret
define i32 @ret0() {
  ret i32 0
}

; CHECK-LABEL: ret_neg1:
; CHECK: mnegl $1, %r0
; CHECK: ret
define i32 @ret_neg1() {
  ret i32 -1
}
