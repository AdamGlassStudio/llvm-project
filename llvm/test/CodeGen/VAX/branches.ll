; RUN: llc -mtriple=vax-unknown-netbsdelf < %s | FileCheck %s

@a = dso_local global i32 10
@b = dso_local global i32 3

; Signed greater-than: LLVM inverts condition for fall-through, so bleq or bgtr.
; CHECK-LABEL: test_bgtr:
; CHECK:       cmpl {{%r[0-9]+}}, {{%r[0-9]+}}
; CHECK:       b{{[a-z]+}}
; CHECK:       ret
define i32 @test_bgtr() {
  %va = load i32, ptr @a
  %vb = load i32, ptr @b
  %cmp = icmp sgt i32 %va, %vb
  br i1 %cmp, label %then, label %else
then:
  ret i32 %va
else:
  ret i32 %vb
}

; Signed less-than.
; CHECK-LABEL: test_blss:
; CHECK:       cmpl {{%r[0-9]+}}, {{%r[0-9]+}}
; CHECK:       b{{[a-z]+}}
; CHECK:       ret
define i32 @test_blss() {
  %va = load i32, ptr @a
  %vb = load i32, ptr @b
  %cmp = icmp slt i32 %va, %vb
  br i1 %cmp, label %then, label %else
then:
  ret i32 %vb
else:
  ret i32 %va
}

; Signed greater-or-equal.
; CHECK-LABEL: test_bgeq:
; CHECK:       cmpl
; CHECK:       b{{[a-z]+}}
; CHECK:       ret
define i32 @test_bgeq() {
  %va = load i32, ptr @a
  %vb = load i32, ptr @b
  %cmp = icmp sge i32 %va, %vb
  br i1 %cmp, label %then, label %else
then:
  ret i32 1
else:
  ret i32 0
}

; Signed less-or-equal.
; CHECK-LABEL: test_bleq:
; CHECK:       cmpl
; CHECK:       b{{[a-z]+}}
; CHECK:       ret
define i32 @test_bleq() {
  %va = load i32, ptr @a
  %vb = load i32, ptr @b
  %cmp = icmp sle i32 %va, %vb
  br i1 %cmp, label %then, label %else
then:
  ret i32 1
else:
  ret i32 0
}

; Compare vs zero: movl sets PSW, so tstl is elided by peephole.
; CHECK-LABEL: test_tstl_eq:
; CHECK:       movl a, %r0
; CHECK-NEXT:  beql
; CHECK:       ret
define i32 @test_tstl_eq() {
  %va = load i32, ptr @a
  %cmp = icmp eq i32 %va, 0
  br i1 %cmp, label %zero, label %nonzero
zero:
  ret i32 -1
nonzero:
  ret i32 %va
}

; Not-equal compare.
; CHECK-LABEL: test_bneq:
; CHECK:       cmpl
; CHECK:       b{{[a-z]+}}
; CHECK:       ret
define i32 @test_bneq() {
  %va = load i32, ptr @a
  %vb = load i32, ptr @b
  %cmp = icmp ne i32 %va, %vb
  br i1 %cmp, label %diff, label %same
diff:
  ret i32 1
same:
  ret i32 0
}

; Unsigned less-than: must use an unsigned branch (blssu or bgequ for invert).
; CHECK-LABEL: test_blssu:
; CHECK:       cmpl
; CHECK:       b{{[a-z]+}}
; CHECK:       ret
define i32 @test_blssu() {
  %va = load i32, ptr @a
  %vb = load i32, ptr @b
  %cmp = icmp ult i32 %va, %vb
  br i1 %cmp, label %yes, label %no
yes:
  ret i32 1
no:
  ret i32 0
}

; Unconditional branch: brw (or optimised away if fall-through).
; CHECK-LABEL: test_brw:
; CHECK:       ret
define i32 @test_brw() {
  br label %dest
dest:
  ret i32 42
}

; Loop: sum 0+1+...+(n-1) using globals.
; CHECK-LABEL: test_loop:
; CHECK:       cmpl
; CHECK:       b{{[a-z]+}}
; CHECK:       incl
; CHECK:       ret
define i32 @test_loop() {
  %n = load i32, ptr @a
  br label %loop_check
loop_check:
  %i = phi i32 [ 0, %0 ], [ %i1, %loop_body ]
  %s = phi i32 [ 0, %0 ], [ %s1, %loop_body ]
  %cmp = icmp slt i32 %i, %n
  br i1 %cmp, label %loop_body, label %loop_exit
loop_body:
  %s1 = add i32 %s, %i
  %i1 = add i32 %i, 1
  br label %loop_check
loop_exit:
  ret i32 %s
}


