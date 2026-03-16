; RUN: llc -mtriple=vax-unknown-netbsdelf < %s | FileCheck %s

; Test ALU memory-operand ISel patterns: compares, unary ops, 3-op ALU,
; and RMW (increment/decrement) with memory operands.

; --- CMPL_mm: compare two memory values directly ---
define i32 @cmp_two_loads(ptr %p, ptr %q) nounwind {
; CHECK-LABEL: cmp_two_loads:
; CHECK:       cmpl {{.*}}, {{.*}}
; CHECK:       bleq
  %a = load i32, ptr %p
  %b = load i32, ptr %q
  %cmp = icmp sgt i32 %a, %b
  %res = select i1 %cmp, i32 1, i32 0
  ret i32 %res
}

; --- MNEGL_mm: negate from memory to memory ---
define void @negate_mem(ptr %src, ptr %dst) nounwind {
; CHECK-LABEL: negate_mem:
; CHECK:       mnegl {{.*}}, {{.*}}
; CHECK-NEXT:  ret
  %val = load i32, ptr %src
  %neg = sub i32 0, %val
  store i32 %neg, ptr %dst
  ret void
}

; --- MCOML_mm: complement from memory to memory ---
define void @complement_mem(ptr %src, ptr %dst) nounwind {
; CHECK-LABEL: complement_mem:
; CHECK:       mcoml {{.*}}, {{.*}}
; CHECK-NEXT:  ret
  %val = load i32, ptr %src
  %not = xor i32 %val, -1
  store i32 %not, ptr %dst
  ret void
}

; --- INCL_m: RMW increment ---
define void @increment_mem(ptr %p) nounwind {
; CHECK-LABEL: increment_mem:
; CHECK:       incl (%r{{[0-9]+}})
; CHECK-NEXT:  ret
  %val = load i32, ptr %p
  %inc = add i32 %val, 1
  store i32 %inc, ptr %p
  ret void
}

; --- DECL_m: RMW decrement ---
define void @decrement_mem(ptr %p) nounwind {
; CHECK-LABEL: decrement_mem:
; CHECK:       decl (%r{{[0-9]+}})
; CHECK-NEXT:  ret
  %val = load i32, ptr %p
  %dec = add i32 %val, -1
  store i32 %dec, ptr %p
  ret void
}

; --- Negative test: multi-use load should NOT fold into INCL_m ---
define i32 @incl_multiuse(ptr %p) nounwind {
; CHECK-LABEL: incl_multiuse:
; CHECK-NOT:   incl (%r
  %val = load i32, ptr %p
  %inc = add i32 %val, 1
  store i32 %inc, ptr %p
  ret i32 %val
}
