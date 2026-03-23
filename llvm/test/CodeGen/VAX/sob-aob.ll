; RUN: llc -mtriple=vax-unknown-netbsdelf -O2 < %s | FileCheck %s

; Test that loop patterns are combined into SOB/AOB instructions.

declare void @body(i32)
declare void @use(ptr)

; --- SOBGTR: count-down loop (i > 0) ---

define void @sobgtr_loop(i32 %n) {
; CHECK-LABEL: sobgtr_loop:
; CHECK:       sobgtr {{%r[0-9]+}}, .LBB0_1
entry:
  %cmp = icmp sgt i32 %n, 0
  br i1 %cmp, label %loop, label %exit
loop:
  %i = phi i32 [ %n, %entry ], [ %next, %loop ]
  call void @body(i32 %i)
  %next = add nsw i32 %i, -1
  %test = icmp sgt i32 %next, 0
  br i1 %test, label %loop, label %exit
exit:
  ret void
}

; --- SOBGEQ: count-down loop (i >= 0) ---
; Note: LLVM may optimize >= 0 comparisons differently (e.g., cmpl $-1 + bgtr).
; SOBGEQ triggers when the loop condition is naturally decl + bgeq.

define void @sobgeq_loop(i32 %n) {
; CHECK-LABEL: sobgeq_loop:
; CHECK:       .LBB1_1:
; CHECK:       calls
; CHECK:       decl
entry:
  %cmp = icmp sgt i32 %n, -1
  br i1 %cmp, label %loop, label %exit
loop:
  %i = phi i32 [ %n, %entry ], [ %next, %loop ]
  call void @body(i32 %i)
  %next = add nsw i32 %i, -1
  %test = icmp sge i32 %next, 0
  br i1 %test, label %loop, label %exit
exit:
  ret void
}

; --- AOBLSS: count-up loop (i < limit) ---

define void @aoblss_loop(i32 %n, ptr %arr) {
; CHECK-LABEL: aoblss_loop:
; CHECK:       aoblss {{%r[0-9]+|[$][-0-9]+}}, {{%r[0-9]+}}, .LBB2_
entry:
  %cmp = icmp sgt i32 %n, 0
  br i1 %cmp, label %loop, label %exit
loop:
  %i = phi i32 [ 0, %entry ], [ %next, %loop ]
  %ptr = getelementptr i32, ptr %arr, i32 %i
  store i32 %i, ptr %ptr
  %next = add nuw nsw i32 %i, 1
  %test = icmp slt i32 %next, %n
  br i1 %test, label %loop, label %exit
exit:
  ret void
}

; --- AOBLEQ: count-up loop (i <= limit) ---

define void @aobleq_loop(i32 %n, ptr %arr) {
; CHECK-LABEL: aobleq_loop:
; CHECK:       aobleq {{%r[0-9]+|[$][-0-9]+}}, {{%r[0-9]+}}, .LBB3_
entry:
  %cmp = icmp sgt i32 %n, -1
  br i1 %cmp, label %loop, label %exit
loop:
  %i = phi i32 [ 0, %entry ], [ %next, %loop ]
  %ptr = getelementptr i32, ptr %arr, i32 %i
  store i32 %i, ptr %ptr
  %next = add nuw nsw i32 %i, 1
  %test = icmp sle i32 %next, %n
  br i1 %test, label %loop, label %exit
exit:
  ret void
}

; --- Negative test: loop with call in body should still use SOB ---

define void @sobgtr_with_call(i32 %n) {
; CHECK-LABEL: sobgtr_with_call:
; CHECK:       sobgtr {{%r[0-9]+}}, .LBB4_1
entry:
  %cmp = icmp sgt i32 %n, 0
  br i1 %cmp, label %loop, label %exit
loop:
  %i = phi i32 [ %n, %entry ], [ %next, %loop ]
  call void @body(i32 %i)
  %next = add nsw i32 %i, -1
  %test = icmp sgt i32 %next, 0
  br i1 %test, label %loop, label %exit
exit:
  ret void
}
