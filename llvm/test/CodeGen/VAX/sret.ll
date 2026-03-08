; RUN: llc -march=vax < %s | FileCheck %s

%struct.pair = type { i32, i32 }

; Callee: receives sret pointer at 4(%ap), stores through it
define void @make_pair(ptr sret(%struct.pair) %out, i32 %x, i32 %y) {
; CHECK-LABEL: make_pair:
; CHECK: movl 4(%ap), %r0
; CHECK: movl 12(%ap), %r1
; CHECK: movl %r1, 4(%r0)
; CHECK: movl 8(%ap), %r1
; CHECK: movl %r1, (%r0)
  %a = getelementptr inbounds %struct.pair, ptr %out, i32 0, i32 0
  store i32 %x, ptr %a
  %b = getelementptr inbounds %struct.pair, ptr %out, i32 0, i32 1
  store i32 %y, ptr %b
  ret void
}

; Caller: allocates struct, passes address as sret arg
define i32 @use_pair() {
; CHECK-LABEL: use_pair:
; CHECK: pushal {{.*}}(%fp)
; CHECK: calls $3, make_pair
  %p = alloca %struct.pair
  call void @make_pair(ptr sret(%struct.pair) %p, i32 3, i32 4)
  %a = getelementptr inbounds %struct.pair, ptr %p, i32 0, i32 0
  %va = load i32, ptr %a
  %b = getelementptr inbounds %struct.pair, ptr %p, i32 0, i32 1
  %vb = load i32, ptr %b
  %r = add i32 %va, %vb
  ret i32 %r
}
