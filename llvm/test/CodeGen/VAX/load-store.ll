; RUN: llc -mtriple=vax-unknown-netbsdelf < %s | FileCheck %s

; Test various load and store patterns

@g8 = external dso_local global i8
@g16 = external dso_local global i16
@g32 = external dso_local global i32

; i8 load returns i32, so zero-extends
define i8 @load_i8() {
; CHECK-LABEL: load_i8:
; CHECK: movzbl g8, %r0
  %v = load i8, ptr @g8
  ret i8 %v
}

; i16 load returns i32, so zero-extends
define i16 @load_i16() {
; CHECK-LABEL: load_i16:
; CHECK: movzwl g16, %r0
  %v = load i16, ptr @g16
  ret i16 %v
}

define i32 @load_i32() {
; CHECK-LABEL: load_i32:
; CHECK: movl g32, %r0
  %v = load i32, ptr @g32
  ret i32 %v
}

define void @store_i8(i8 %v) {
; CHECK-LABEL: store_i8:
; CHECK: movb %r0, g8
  store i8 %v, ptr @g8
  ret void
}

define void @store_i16(i16 %v) {
; CHECK-LABEL: store_i16:
; CHECK: movw %r0, g16
  store i16 %v, ptr @g16
  ret void
}

define void @store_i32(i32 %v) {
; CHECK-LABEL: store_i32:
; CHECK: movl 4(%ap), g32
  store i32 %v, ptr @g32
  ret void
}

define i32 @load_volatile() {
; CHECK-LABEL: load_volatile:
; CHECK: movl g32, %r0
  %v = load volatile i32, ptr @g32
  ret i32 %v
}

define void @store_volatile(i32 %v) {
; CHECK-LABEL: store_volatile:
; Volatile stores go through a register to avoid scheduling cycle issues with
; nonvolatile PatFrags (volatile loads/stores are not folded into compound
; mem-to-mem instructions).
; CHECK: movl 4(%ap), %r0
; CHECK-NEXT: movl %r0, g32
  store volatile i32 %v, ptr @g32
  ret void
}

; Load from pointer argument with displacement
define i32 @load_disp(ptr %p) {
; CHECK-LABEL: load_disp:
; CHECK: movl 4(%ap), %r0
; CHECK: movl 8(%r0), %r0
  %gep = getelementptr i32, ptr %p, i32 2
  %v = load i32, ptr %gep
  ret i32 %v
}

; Store to pointer with displacement
define void @store_disp(ptr %p, i32 %v) {
; CHECK-LABEL: store_disp:
; CHECK: movl 4(%ap), %r0
; CHECK: movl 8(%ap), 12(%r0)
  %gep = getelementptr i32, ptr %p, i32 3
  store i32 %v, ptr %gep
  ret void
}
