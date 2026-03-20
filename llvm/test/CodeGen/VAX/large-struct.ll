; RUN: llc -mtriple=vax-unknown-netbsdelf < %s | FileCheck %s

; Test large struct passing by value (>= 8 bytes).
; VAX CALLS convention passes large structs on the stack.

%struct.large = type { i32, i32, i32, i32 }

declare void @consume(%struct.large)
declare void @consume_byval(ptr byval(%struct.large))

define void @pass_large_struct(ptr %p) {
; CHECK-LABEL: pass_large_struct:
; CHECK: calls $4, consume@PLT
  %s = load %struct.large, ptr %p
  call void @consume(%struct.large %s)
  ret void
}

define void @pass_byval(ptr %p) {
; CHECK-LABEL: pass_byval:
; CHECK: calls
  call void @consume_byval(ptr byval(%struct.large) %p)
  ret void
}

define i32 @return_struct_member(ptr %p) {
; CHECK-LABEL: return_struct_member:
; CHECK: movl 4(%ap), %r0
; CHECK: movl 8(%r0), %r0
; CHECK: ret
  %gep = getelementptr %struct.large, ptr %p, i32 0, i32 2
  %v = load i32, ptr %gep
  ret i32 %v
}
