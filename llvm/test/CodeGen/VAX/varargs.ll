; RUN: llc -march=vax < %s | FileCheck %s

declare void @llvm.va_start(ptr)
declare void @llvm.va_end(ptr)

; Variadic callee: sum of N variadic i32 args.
define i32 @sum_varargs(i32 %count, ...) {
; CHECK-LABEL: sum_varargs:
; CHECK:       addl3 $8, %ap
; CHECK:       movl 4(%ap)
; CHECK:       ret
entry:
  %ap = alloca ptr
  call void @llvm.va_start(ptr %ap)
  %sum = alloca i32
  store i32 0, ptr %sum
  br label %loop
loop:
  %i = phi i32 [0, %entry], [%i.next, %loop]
  %s = load i32, ptr %sum
  %vp = load ptr, ptr %ap
  %val = load i32, ptr %vp
  %vp.next = getelementptr i8, ptr %vp, i32 4
  store ptr %vp.next, ptr %ap
  %s.new = add i32 %s, %val
  store i32 %s.new, ptr %sum
  %i.next = add i32 %i, 1
  %done = icmp eq i32 %i.next, %count
  br i1 %done, label %exit, label %loop
exit:
  call void @llvm.va_end(ptr %ap)
  %result = load i32, ptr %sum
  ret i32 %result
}

; Variadic call site: pass 3 variadic args.
define i32 @call_varargs() {
; CHECK-LABEL: call_varargs:
; CHECK:       pushl $12
; CHECK:       pushl $20
; CHECK:       pushl $10
; CHECK:       pushl $3
; CHECK:       calls $4, sum_varargs
; CHECK:       ret
  %r = call i32 (i32, ...) @sum_varargs(i32 3, i32 10, i32 20, i32 12)
  ret i32 %r
}
