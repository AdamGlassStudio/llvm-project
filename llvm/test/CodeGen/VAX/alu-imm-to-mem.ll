; RUN: llc -mtriple=vax-unknown-netbsdelf < %s | FileCheck %s

; Test immediate-to-memory ISel patterns: compare memory against immediate,
; and RMW ALU operations with immediate source and memory destination.

@counter = dso_local global i32 0
@flags = dso_local global i32 0

; --- CMPL_mi: compare memory against immediate ---
define i32 @cmp_mem_imm() nounwind {
; CHECK-LABEL: cmp_mem_imm:
; CHECK:       cmpl counter, $42
  %val = load i32, ptr @counter
  %cmp = icmp sgt i32 %val, 42
  %res = select i1 %cmp, i32 1, i32 0
  ret i32 %res
}

; --- CMPL_mi: compare with load from pointer ---
define i32 @cmp_load_imm(ptr %p) nounwind {
; CHECK-LABEL: cmp_load_imm:
; CHECK:       cmpl ({{.*}}), $100
  %val = load i32, ptr %p
  %cmp = icmp sgt i32 %val, 100
  %res = select i1 %cmp, i32 1, i32 0
  ret i32 %res
}

; --- ADDL2_mi: mem += immediate ---
define void @add_imm_to_mem() nounwind {
; CHECK-LABEL: add_imm_to_mem:
; CHECK:       addl2 $5, counter
; CHECK-NEXT:  ret
  %val = load i32, ptr @counter
  %add = add i32 %val, 5
  store i32 %add, ptr @counter
  ret void
}

; --- SUBL2_mi: mem -= immediate (canonicalized to addl2 of negative) ---
define void @sub_imm_from_mem() nounwind {
; CHECK-LABEL: sub_imm_from_mem:
; CHECK:       addl2 $-3, counter
; CHECK-NEXT:  ret
  %val = load i32, ptr @counter
  %sub = sub i32 %val, 3
  store i32 %sub, ptr @counter
  ret void
}

; --- BISL2_mi: mem |= immediate ---
define void @or_imm_to_mem() nounwind {
; CHECK-LABEL: or_imm_to_mem:
; CHECK:       bisl2 $255, flags
; CHECK-NEXT:  ret
  %val = load i32, ptr @flags
  %or = or i32 %val, 255
  store i32 %or, ptr @flags
  ret void
}

; --- BICL2_mi: mem &= ~immediate ---
define void @bic_imm_from_mem() nounwind {
; CHECK-LABEL: bic_imm_from_mem:
; CHECK:       bicl2 $15, flags
; CHECK-NEXT:  ret
  %val = load i32, ptr @flags
  %not = and i32 %val, -16
  store i32 %not, ptr @flags
  ret void
}

; --- XORL2_mi: mem ^= immediate ---
define void @xor_imm_to_mem() nounwind {
; CHECK-LABEL: xor_imm_to_mem:
; CHECK:       xorl2 $4096, flags
; CHECK-NEXT:  ret
  %val = load i32, ptr @flags
  %xor = xor i32 %val, 4096
  store i32 %xor, ptr @flags
  ret void
}

; --- ADDL2_mi via pointer: *(p) += 10 ---
define void @add_imm_to_ptr(ptr %p) nounwind {
; CHECK-LABEL: add_imm_to_ptr:
; CHECK:       addl2 $10, ({{.*}})
; CHECK-NEXT:  ret
  %val = load i32, ptr %p
  %add = add i32 %val, 10
  store i32 %add, ptr %p
  ret void
}
