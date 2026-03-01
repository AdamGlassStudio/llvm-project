; RUN: llc -march=vax < %s | FileCheck %s

; Test switch statement lowering: small switch (branch chain) and
; large switch (jump table).

; Small switch — lowered as compare-and-branch chain.
define i32 @test_switch_small(i32 %x) {
; CHECK-LABEL: test_switch_small:
; CHECK:       cmpl
; CHECK:       beql
; CHECK:       ret
entry:
  switch i32 %x, label %default [
    i32 0, label %case0
    i32 1, label %case1
  ]
case0: ret i32 10
case1: ret i32 20
default: ret i32 99
}

; Large switch — lowered via jump table.
define i32 @test_switch_jt(i32 %x) {
; CHECK-LABEL: test_switch_jt:
; CHECK:       cmpl {{.*}}, $4
; CHECK:       bgtru
; CHECK:       ashl $2
; CHECK:       moval .LJTI
; CHECK:       jmp
; CHECK:       .long .LBB
entry:
  switch i32 %x, label %default [
    i32 0, label %case0
    i32 1, label %case1
    i32 2, label %case2
    i32 3, label %case3
    i32 4, label %case4
  ]
case0: ret i32 10
case1: ret i32 20
case2: ret i32 30
case3: ret i32 40
case4: ret i32 50
default: ret i32 99
}
