; RUN: llc -mtriple=vax-unknown-netbsdelf < %s | FileCheck %s

; Test complex control flow patterns

; Nested if/else
define i32 @nested_if(i32 %a, i32 %b, i32 %c) {
; CHECK-LABEL: nested_if:
; CHECK: cmpl
; CHECK: b{{[a-z]+}}
entry:
  %c1 = icmp sgt i32 %a, 0
  br i1 %c1, label %then1, label %else1

then1:
  %c2 = icmp sgt i32 %b, 0
  br i1 %c2, label %then2, label %else2

then2:
  %r1 = add i32 %a, %b
  br label %end

else2:
  %r2 = sub i32 %a, %b
  br label %end

else1:
  %r3 = mul i32 %a, %c
  br label %end

end:
  %r = phi i32 [ %r1, %then2 ], [ %r2, %else2 ], [ %r3, %else1 ]
  ret i32 %r
}

; While loop with multiple exit conditions
define i32 @while_loop(i32 %n) {
; CHECK-LABEL: while_loop:
; CHECK: cmpl
entry:
  br label %loop

loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop.inc ]
  %sum = phi i32 [ 0, %entry ], [ %sum.next, %loop.inc ]
  %cond = icmp slt i32 %i, %n
  br i1 %cond, label %loop.inc, label %exit

loop.inc:
  %sum.next = add i32 %sum, %i
  %i.next = add i32 %i, 1
  br label %loop

exit:
  ret i32 %sum
}

; Do-while style (loop with backedge test)
define i32 @do_while(i32 %n) {
; CHECK-LABEL: do_while:
entry:
  br label %body

body:
  %i = phi i32 [ 0, %entry ], [ %i.next, %body ]
  %acc = phi i32 [ 1, %entry ], [ %acc.next, %body ]
  %acc.next = mul i32 %acc, %i
  %i.next = add i32 %i, 1
  %cond = icmp slt i32 %i.next, %n
  br i1 %cond, label %body, label %exit

exit:
  ret i32 %acc.next
}

; Diamond pattern (if/else merge)
define i32 @diamond(i32 %a, i32 %b) {
; CHECK-LABEL: diamond:
; CHECK: cmpl
entry:
  %cond = icmp sgt i32 %a, %b
  br i1 %cond, label %left, label %right

left:
  %l = add i32 %a, 1
  br label %merge

right:
  %r = add i32 %b, 1
  br label %merge

merge:
  %result = phi i32 [ %l, %left ], [ %r, %right ]
  ret i32 %result
}

; Counted loop (common pattern)
define i32 @counted_loop(ptr %arr, i32 %n) {
; CHECK-LABEL: counted_loop:
entry:
  br label %loop

loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %sum = phi i32 [ 0, %entry ], [ %sum.next, %loop ]
  %ptr = getelementptr i32, ptr %arr, i32 %i
  %val = load i32, ptr %ptr
  %sum.next = add i32 %sum, %val
  %i.next = add i32 %i, 1
  %cond = icmp slt i32 %i.next, %n
  br i1 %cond, label %loop, label %exit

exit:
  ret i32 %sum.next
}
