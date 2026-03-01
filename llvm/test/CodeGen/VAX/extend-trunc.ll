; RUN: llc -mtriple=vax-unknown-netbsdelf < %s | FileCheck %s

@sb = global i8  -5      ; signed byte
@ub = global i8   5      ; unsigned byte
@sw = global i16 -1000   ; signed word
@uw = global i16  1000   ; unsigned word
@dl = global i32  42     ; longword (destination for stores)

; sextloadi8: signed char loaded → sign-extended to i32  (CVTBL)
; CHECK-LABEL: test_sextloadi8:
; CHECK:       cvtbl
; CHECK:       ret
define i32 @test_sextloadi8() {
  %v = load i8, ptr @sb
  %r = sext i8 %v to i32
  ret i32 %r
}

; zextloadi8: unsigned char loaded → zero-extended to i32  (MOVZBL)
; CHECK-LABEL: test_zextloadi8:
; CHECK:       movzbl
; CHECK:       ret
define i32 @test_zextloadi8() {
  %v = load i8, ptr @ub
  %r = zext i8 %v to i32
  ret i32 %r
}

; sextloadi16: signed short loaded → sign-extended to i32  (CVTWL)
; CHECK-LABEL: test_sextloadi16:
; CHECK:       cvtwl
; CHECK:       ret
define i32 @test_sextloadi16() {
  %v = load i16, ptr @sw
  %r = sext i16 %v to i32
  ret i32 %r
}

; zextloadi16: unsigned short loaded → zero-extended to i32  (MOVZWL)
; CHECK-LABEL: test_zextloadi16:
; CHECK:       movzwl
; CHECK:       ret
define i32 @test_zextloadi16() {
  %v = load i16, ptr @uw
  %r = zext i16 %v to i32
  ret i32 %r
}

; truncstorei8: store low byte to memory  (MOVB)
; CHECK-LABEL: test_truncstorei8:
; CHECK:       movb
; CHECK:       ret
define void @test_truncstorei8() {
  %v = load i32, ptr @dl
  %t = trunc i32 %v to i8
  store i8 %t, ptr @sb
  ret void
}

; truncstorei16: store low word to memory  (MOVW)
; CHECK-LABEL: test_truncstorei16:
; CHECK:       movw
; CHECK:       ret
define void @test_truncstorei16() {
  %v = load i32, ptr @dl
  %t = trunc i32 %v to i16
  store i16 %t, ptr @sw
  ret void
}

; sext_inreg i8: sign-extend result of byte arithmetic already in register (CVTBL)
; CHECK-LABEL: test_sext_inreg_i8:
; CHECK:       cvtbl
; CHECK:       ret
define i32 @test_sext_inreg_i8() {
  %a = load i8, ptr @sb
  %b = load i8, ptr @ub
  %s = add i8 %a, %b        ; add as i8, then sext result
  %r = sext i8 %s to i32
  ret i32 %r
}

; sext_inreg i16: sign-extend from low 16 bits (CVTWL)
; CHECK-LABEL: test_sext_inreg_i16:
; CHECK:       cvtwl
; CHECK:       ret
define i32 @test_sext_inreg_i16() {
  %a = load i16, ptr @sw
  %b = load i16, ptr @uw
  %s = add i16 %a, %b
  %r = sext i16 %s to i32
  ret i32 %r
}
