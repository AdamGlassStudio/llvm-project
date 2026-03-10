; RUN: llc -mtriple=vax-unknown-netbsdelf < %s | FileCheck %s
;
; Bit manipulation intrinsics.
; VAX has no native CLZ, bswap, or popcount — these expand inline.
; CTZ uses VAX FFS instruction.

declare i32 @llvm.ctlz.i32(i32, i1)
declare i32 @llvm.cttz.i32(i32, i1)
declare i32 @llvm.bswap.i32(i32)
declare i32 @llvm.ctpop.i32(i32)

define i32 @clz(i32 %x) {
; CHECK-LABEL: clz:
; CHECK:       tstl %r{{[0-9]+}}
; CHECK:       bneq
; CHECK:       extzv
; CHECK:       mcoml
; CHECK:       ret
  %r = call i32 @llvm.ctlz.i32(i32 %x, i1 false)
  ret i32 %r
}

; CTZ uses FFS instruction (with zero check since cttz(0) = 32).
define i32 @ctz(i32 %x) {
; CHECK-LABEL: ctz:
; CHECK:       movl 4(%ap), %r{{[0-9]+}}
; CHECK:       tstl %r{{[0-9]+}}
; CHECK:       beql
; CHECK:       ffs $0, $32, %r{{[0-9]+}}, %r0
; CHECK:       ret
  %r = call i32 @llvm.cttz.i32(i32 %x, i1 false)
  ret i32 %r
}

define i32 @bswap(i32 %x) {
; CHECK-LABEL: bswap:
; CHECK:       movl 4(%ap), %r0
; CHECK:       extzv $24, $8, %r0
; CHECK:       bisl2
; CHECK:       ret
  %r = call i32 @llvm.bswap.i32(i32 %x)
  ret i32 %r
}

define i32 @popcount(i32 %x) {
; CHECK-LABEL: popcount:
; CHECK:       movl 4(%ap), %r0
; CHECK:       extzv $1, $31
; CHECK:       mull2 $16843009
; CHECK:       extzv $24, $8, %r0, %r0
; CHECK-NEXT:  ret
  %r = call i32 @llvm.ctpop.i32(i32 %x)
  ret i32 %r
}
