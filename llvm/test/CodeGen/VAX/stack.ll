; RUN: llc -mtriple=vax-unknown-netbsdelf < %s | FileCheck %s

; Test stack allocation patterns

; Static alloca
define i32 @static_alloca() {
; CHECK-LABEL: static_alloca:
; CHECK: subl2
  %a = alloca i32
  store i32 42, ptr %a
  %v = load i32, ptr %a
  ret i32 %v
}

; Large stack frame
define i32 @large_frame() {
; CHECK-LABEL: large_frame:
; CHECK: subl2
  %a = alloca [256 x i32]
  %p = getelementptr [256 x i32], ptr %a, i32 0, i32 0
  store i32 1, ptr %p
  %p2 = getelementptr [256 x i32], ptr %a, i32 0, i32 255
  store i32 2, ptr %p2
  %v = load i32, ptr %p
  ret i32 %v
}

; Multiple allocas
define i32 @multi_alloca() {
; CHECK-LABEL: multi_alloca:
  %a = alloca i32
  %b = alloca i32
  %c = alloca i32
  store i32 10, ptr %a
  store i32 20, ptr %b
  store i32 30, ptr %c
  %va = load i32, ptr %a
  %vb = load i32, ptr %b
  %sum = add i32 %va, %vb
  ret i32 %sum
}

; Array access via alloca
define i32 @array_alloca(i32 %idx) {
; CHECK-LABEL: array_alloca:
; CHECK: subl2
  %arr = alloca [10 x i32]
  %p0 = getelementptr [10 x i32], ptr %arr, i32 0, i32 0
  store i32 100, ptr %p0
  %p1 = getelementptr [10 x i32], ptr %arr, i32 0, i32 1
  store i32 200, ptr %p1
  %pi = getelementptr [10 x i32], ptr %arr, i32 0, i32 %idx
  %v = load i32, ptr %pi
  ret i32 %v
}
