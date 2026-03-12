; RUN: llc -mtriple=vax-unknown-netbsdelf < %s | FileCheck %s

; Test load folding into 3-operand ALU instructions (Phase 32B).
; The ISel folds a load operand directly into the ALU instruction
; instead of emitting a separate MOVL + 2-operand op.
;
; Key pattern: when one operand is a register and the other comes from
; memory (e.g., stack argument or pointer dereference), the 3-operand
; form reads from memory directly: addl3 mem, %reg, %dst

; --- ADDL3: fold stack argument load (commutative) ---

define i32 @add_reg_mem(i32 %a, ptr %p) nounwind {
; CHECK-LABEL: add_reg_mem:
; CHECK:       addl2 4(%ap), %r0
  %val = load i32, ptr %p
  %sum = add i32 %a, %val
  ret i32 %sum
}

define i32 @add_mem_reg(ptr %p, i32 %b) nounwind {
; CHECK-LABEL: add_mem_reg:
; CHECK:       addl2 (%r{{[0-9]+}}), %r0
  %val = load i32, ptr %p
  %sum = add i32 %val, %b
  ret i32 %sum
}

; --- MULL3: fold stack argument load (commutative) ---

define i32 @mul_reg_mem(i32 %a, ptr %p) nounwind {
; CHECK-LABEL: mul_reg_mem:
; CHECK:       mull2 4(%ap), %r0
  %val = load i32, ptr %p
  %sum = mul i32 %a, %val
  ret i32 %sum
}

; --- BISL3: fold stack argument load (commutative) ---

define i32 @or_reg_mem(i32 %a, ptr %p) nounwind {
; CHECK-LABEL: or_reg_mem:
; CHECK:       bisl2 4(%ap), %r0
  %val = load i32, ptr %p
  %res = or i32 %a, %val
  ret i32 %res
}

; --- XORL3: fold stack argument load (commutative) ---

define i32 @xor_reg_mem(i32 %a, ptr %p) nounwind {
; CHECK-LABEL: xor_reg_mem:
; CHECK:       xorl2 4(%ap), %r0
  %val = load i32, ptr %p
  %res = xor i32 %a, %val
  ret i32 %res
}

; --- SUBL3: non-commutative, need both orderings ---

; sub(reg, load(mem)) → subl3 mem, reg, dst
; The loaded value is the subtrahend (second operand of sub)
define i32 @sub_reg_mem(i32 %a, ptr %p) nounwind {
; CHECK-LABEL: sub_reg_mem:
; CHECK:       subl3 (%r{{[0-9]+}}), %r{{[0-9]+}}, %r0
  %val = load i32, ptr %p
  %res = sub i32 %a, %val
  ret i32 %res
}

; sub(load(mem), reg) → subl3 reg, mem, dst
; The loaded value is the minuend (first operand of sub)
define i32 @sub_mem_reg(ptr %p, i32 %b) nounwind {
; CHECK-LABEL: sub_mem_reg:
; CHECK:       subl3 8(%ap), %r{{[0-9]+}}, %r0
  %val = load i32, ptr %p
  %res = sub i32 %val, %b
  ret i32 %res
}

; --- CMPL: fold pointer dereference into compare ---

define i32 @cmp_mem_reg(ptr %p, i32 %b) nounwind {
; CHECK-LABEL: cmp_mem_reg:
; CHECK:       cmpl (%r{{[0-9]+}}), 8(%ap)
  %val = load i32, ptr %p
  %cmp = icmp sgt i32 %val, %b
  %res = select i1 %cmp, i32 1, i32 0
  ret i32 %res
}

; --- Stack argument folding: both args from AP ---

define i32 @add_stack_args(i32 %a, i32 %b) nounwind {
; CHECK-LABEL: add_stack_args:
; CHECK:       addl2 4(%ap), %r0
; CHECK-NEXT:  ret
  %sum = add i32 %a, %b
  ret i32 %sum
}
