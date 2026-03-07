; RUN: llc -march=vax -O2 < %s | FileCheck %s

; Test indexed addressing mode for longword (4-byte) array access.
; VAX indexed mode: base[Rx] computes EA = EA(base) + Rx * data_size.
; For MOVL (longword), data_size = 4, so SHL(idx, 2) maps to indexed mode.

; Simple array load: a[i]
define i32 @load_array(ptr %a, i32 %i) {
; CHECK-LABEL: load_array:
; CHECK: movl (%r{{[0-9]+}})[%r{{[0-9]+}}], %r0
entry:
  %ptr = getelementptr i32, ptr %a, i32 %i
  %val = load i32, ptr %ptr
  ret i32 %val
}

; Simple array store: a[i] = val
define void @store_array(ptr %a, i32 %i, i32 %val) {
; CHECK-LABEL: store_array:
; CHECK: movl %r{{[0-9]+}}, (%r{{[0-9]+}})[%r{{[0-9]+}}]
entry:
  %ptr = getelementptr i32, ptr %a, i32 %i
  store i32 %val, ptr %ptr
  ret void
}

; Array load with constant offset: s->field[i] (struct base + offset + index).
; DAGCombiner may reorder the additions; indexed + displacement is ideal but
; the optimizer may compute the index separately and use plain displacement.
define i32 @load_array_offset(ptr %base, i32 %i) {
; CHECK-LABEL: load_array_offset:
; CHECK: movl
; CHECK: ret
entry:
  %offset = getelementptr i8, ptr %base, i32 16
  %arr = bitcast ptr %offset to ptr
  %ptr = getelementptr i32, ptr %arr, i32 %i
  %val = load i32, ptr %ptr
  ret i32 %val
}

; Verify byte loads do NOT use indexed mode (scale mismatch).
; MOVZBL uses byte scale (1), but SHL by 2 is longword scale (4).
; The compiler should use explicit address arithmetic, not indexed.
define i32 @load_byte_array(ptr %a, i32 %i) {
; CHECK-LABEL: load_byte_array:
; CHECK-NOT: movzbl {{.*}}[{{%r[0-9]+}}]
; CHECK: movzbl
entry:
  %ptr = getelementptr i8, ptr %a, i32 %i
  %byte = load i8, ptr %ptr
  %val = zext i8 %byte to i32
  ret i32 %val
}
