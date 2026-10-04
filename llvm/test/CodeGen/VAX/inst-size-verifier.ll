; RUN: llc -mtriple=vax-unknown-netbsdelf < %s | FileCheck %s
; RUN: llc -mtriple=vax-unknown-netbsdelf -filetype=obj -o /dev/null %s

; Exercise instructions with operands synthesized by the asm printer. Their
; reported maximum size must include those operands.

declare void @sink(i32, i32)
declare void @side_effect(i32)

define void @push_ashl() {
; CHECK-LABEL: push_ashl:
; CHECK:       ashl $11, $1, -(%sp)
  call void @sink(i32 2048, i32 0)
  ret void
}

define void @push_mnegl() {
; CHECK-LABEL: push_mnegl:
; CHECK:       mnegl $1, -(%sp)
  call void @sink(i32 -1, i32 0)
  ret void
}

define void @large_casel(i32 %x) {
; CHECK-LABEL: large_casel:
; CHECK:       casel %r0, $0, $64
entry:
  switch i32 %x, label %default [
    i32 0, label %case0
    i32 1, label %case1
    i32 2, label %case2
    i32 3, label %case3
    i32 4, label %case4
    i32 5, label %case5
    i32 6, label %case6
    i32 7, label %case7
    i32 8, label %case8
    i32 9, label %case9
    i32 10, label %case10
    i32 11, label %case11
    i32 12, label %case12
    i32 13, label %case13
    i32 14, label %case14
    i32 15, label %case15
    i32 16, label %case16
    i32 17, label %case17
    i32 18, label %case18
    i32 19, label %case19
    i32 20, label %case20
    i32 21, label %case21
    i32 22, label %case22
    i32 23, label %case23
    i32 24, label %case24
    i32 25, label %case25
    i32 26, label %case26
    i32 27, label %case27
    i32 28, label %case28
    i32 29, label %case29
    i32 30, label %case30
    i32 31, label %case31
    i32 32, label %case32
    i32 33, label %case33
    i32 34, label %case34
    i32 35, label %case35
    i32 36, label %case36
    i32 37, label %case37
    i32 38, label %case38
    i32 39, label %case39
    i32 40, label %case40
    i32 41, label %case41
    i32 42, label %case42
    i32 43, label %case43
    i32 44, label %case44
    i32 45, label %case45
    i32 46, label %case46
    i32 47, label %case47
    i32 48, label %case48
    i32 49, label %case49
    i32 50, label %case50
    i32 51, label %case51
    i32 52, label %case52
    i32 53, label %case53
    i32 54, label %case54
    i32 55, label %case55
    i32 56, label %case56
    i32 57, label %case57
    i32 58, label %case58
    i32 59, label %case59
    i32 60, label %case60
    i32 61, label %case61
    i32 62, label %case62
    i32 63, label %case63
    i32 64, label %case64
  ]
case0:
  tail call void @side_effect(i32 0)
  ret void
case1:
  tail call void @side_effect(i32 1)
  ret void
case2:
  tail call void @side_effect(i32 2)
  ret void
case3:
  tail call void @side_effect(i32 3)
  ret void
case4:
  tail call void @side_effect(i32 4)
  ret void
case5:
  tail call void @side_effect(i32 5)
  ret void
case6:
  tail call void @side_effect(i32 6)
  ret void
case7:
  tail call void @side_effect(i32 7)
  ret void
case8:
  tail call void @side_effect(i32 8)
  ret void
case9:
  tail call void @side_effect(i32 9)
  ret void
case10:
  tail call void @side_effect(i32 10)
  ret void
case11:
  tail call void @side_effect(i32 11)
  ret void
case12:
  tail call void @side_effect(i32 12)
  ret void
case13:
  tail call void @side_effect(i32 13)
  ret void
case14:
  tail call void @side_effect(i32 14)
  ret void
case15:
  tail call void @side_effect(i32 15)
  ret void
case16:
  tail call void @side_effect(i32 16)
  ret void
case17:
  tail call void @side_effect(i32 17)
  ret void
case18:
  tail call void @side_effect(i32 18)
  ret void
case19:
  tail call void @side_effect(i32 19)
  ret void
case20:
  tail call void @side_effect(i32 20)
  ret void
case21:
  tail call void @side_effect(i32 21)
  ret void
case22:
  tail call void @side_effect(i32 22)
  ret void
case23:
  tail call void @side_effect(i32 23)
  ret void
case24:
  tail call void @side_effect(i32 24)
  ret void
case25:
  tail call void @side_effect(i32 25)
  ret void
case26:
  tail call void @side_effect(i32 26)
  ret void
case27:
  tail call void @side_effect(i32 27)
  ret void
case28:
  tail call void @side_effect(i32 28)
  ret void
case29:
  tail call void @side_effect(i32 29)
  ret void
case30:
  tail call void @side_effect(i32 30)
  ret void
case31:
  tail call void @side_effect(i32 31)
  ret void
case32:
  tail call void @side_effect(i32 32)
  ret void
case33:
  tail call void @side_effect(i32 33)
  ret void
case34:
  tail call void @side_effect(i32 34)
  ret void
case35:
  tail call void @side_effect(i32 35)
  ret void
case36:
  tail call void @side_effect(i32 36)
  ret void
case37:
  tail call void @side_effect(i32 37)
  ret void
case38:
  tail call void @side_effect(i32 38)
  ret void
case39:
  tail call void @side_effect(i32 39)
  ret void
case40:
  tail call void @side_effect(i32 40)
  ret void
case41:
  tail call void @side_effect(i32 41)
  ret void
case42:
  tail call void @side_effect(i32 42)
  ret void
case43:
  tail call void @side_effect(i32 43)
  ret void
case44:
  tail call void @side_effect(i32 44)
  ret void
case45:
  tail call void @side_effect(i32 45)
  ret void
case46:
  tail call void @side_effect(i32 46)
  ret void
case47:
  tail call void @side_effect(i32 47)
  ret void
case48:
  tail call void @side_effect(i32 48)
  ret void
case49:
  tail call void @side_effect(i32 49)
  ret void
case50:
  tail call void @side_effect(i32 50)
  ret void
case51:
  tail call void @side_effect(i32 51)
  ret void
case52:
  tail call void @side_effect(i32 52)
  ret void
case53:
  tail call void @side_effect(i32 53)
  ret void
case54:
  tail call void @side_effect(i32 54)
  ret void
case55:
  tail call void @side_effect(i32 55)
  ret void
case56:
  tail call void @side_effect(i32 56)
  ret void
case57:
  tail call void @side_effect(i32 57)
  ret void
case58:
  tail call void @side_effect(i32 58)
  ret void
case59:
  tail call void @side_effect(i32 59)
  ret void
case60:
  tail call void @side_effect(i32 60)
  ret void
case61:
  tail call void @side_effect(i32 61)
  ret void
case62:
  tail call void @side_effect(i32 62)
  ret void
case63:
  tail call void @side_effect(i32 63)
  ret void
case64:
  tail call void @side_effect(i32 64)
  ret void
default:
  ret void
}
