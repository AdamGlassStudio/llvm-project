; RUN: llc -mtriple=vax-unknown-netbsdelf < %s | FileCheck %s
;
; Bit manipulation intrinsics expanded inline.
; VAX has no native CLZ, bswap, or popcount instructions.
; CTZ could use VAX FFS in the future (optimization TODO).

declare i32 @llvm.ctlz.i32(i32, i1)
declare i32 @llvm.cttz.i32(i32, i1)
declare i32 @llvm.bswap.i32(i32)
declare i32 @llvm.ctpop.i32(i32)

define i32 @clz(i32 %x) {
; CHECK-LABEL: clz:
; CHECK:       movl 4(%ap), %r1
; CHECK:       movl $32, %r0
; CHECK:       tstl %r1
; CHECK:       beql .LBB0_2
; CHECK:       .LBB0_1:
; CHECK:       rotl
; CHECK:       mcoml
; CHECK:       .LBB0_2:
; CHECK:       ret
  %r = call i32 @llvm.ctlz.i32(i32 %x, i1 false)
  ret i32 %r
}

define i32 @ctz(i32 %x) {
; CHECK-LABEL: ctz:
; CHECK:       movl 4(%ap), %r1
; CHECK:       movl $32, %r0
; CHECK:       tstl %r1
; CHECK:       beql .LBB1_2
; CHECK:       .LBB1_1:
; CHECK:       decl %r0
; CHECK:       bicl3
; CHECK:       mull3
; CHECK:       movzbl
; CHECK:       .LBB1_2:
; CHECK:       ret
  %r = call i32 @llvm.cttz.i32(i32 %x, i1 false)
  ret i32 %r
}

define i32 @bswap(i32 %x) {
; CHECK-LABEL: bswap:
; CHECK:       movl 4(%ap), %r0
; CHECK:       ashl $24, %r0, %r1
; CHECK:       bicl3
; CHECK:       ashl $8
; CHECK:       bisl3
; CHECK:       rotl $8
; CHECK:       rotl $24
; CHECK:       ret
  %r = call i32 @llvm.bswap.i32(i32 %x)
  ret i32 %r
}

define i32 @popcount(i32 %x) {
; CHECK-LABEL: popcount:
; CHECK:       movl 4(%ap), %r0
; CHECK:       rotl $31
; CHECK:       bicl3
; CHECK:       subl3
; CHECK:       mull3 $16843009
; CHECK:       rotl $8
; CHECK:       bicl3 $-256
; CHECK:       ret
  %r = call i32 @llvm.ctpop.i32(i32 %x)
  ret i32 %r
}
