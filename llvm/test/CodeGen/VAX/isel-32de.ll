; RUN: llc -mtriple=vax-unknown-netbsdelf < %s | FileCheck %s

; Test Phase 32D (CLRL store-zero) and 32E (EXTZV, FFS) patterns.

;; --- 32D: CLRL for store zero ---

define void @store_zero_ptr(ptr %p) nounwind {
; CHECK-LABEL: store_zero_ptr:
; CHECK:       clrl (%r{{[0-9]+}})
; CHECK-NEXT:  ret
  store i32 0, ptr %p
  ret void
}

define void @store_zero_disp(ptr %p) nounwind {
; CHECK-LABEL: store_zero_disp:
; CHECK:       clrl 4(%r{{[0-9]+}})
  %q = getelementptr i32, ptr %p, i32 1
  store i32 0, ptr %q
  ret void
}

;; --- 32E: EXTZV for constant logical right shift ---

define i32 @lshr_by_1(i32 %a) nounwind {
; CHECK-LABEL: lshr_by_1:
; CHECK:       extzv $1, $31, {{.*}}, %r0
; CHECK-NEXT:  ret
  %r = lshr i32 %a, 1
  ret i32 %r
}

define i32 @lshr_by_16(i32 %a) nounwind {
; CHECK-LABEL: lshr_by_16:
; CHECK:       movzwl 6(%ap), %r0
; CHECK-NEXT:  ret
  %r = lshr i32 %a, 16
  ret i32 %r
}

define i32 @lshr_by_31(i32 %a) nounwind {
; CHECK-LABEL: lshr_by_31:
; CHECK:       extzv $31, $1, {{.*}}, %r0
; CHECK-NEXT:  ret
  %r = lshr i32 %a, 31
  ret i32 %r
}

;; --- 32E: EXTZV for variable logical right shift ---

define i32 @lshr_var(i32 %a, i32 %b) nounwind {
; CHECK-LABEL: lshr_var:
; CHECK:       subl3
; CHECK:       extzv %r{{[0-9]+}}, %r{{[0-9]+}}, %r{{[0-9]+}}, %r0
; CHECK-NEXT:  ret
  %r = lshr i32 %a, %b
  ret i32 %r
}

;; --- 32E: EXTZV with memory operand (load folded) ---

define i32 @lshr_from_mem(ptr %p) nounwind {
; CHECK-LABEL: lshr_from_mem:
; CHECK:       extzv $4, $28, (%r{{[0-9]+}}), %r0
; CHECK-NEXT:  ret
  %val = load i32, ptr %p
  %r = lshr i32 %val, 4
  ret i32 %r
}

;; --- 32E: FFS for cttz_zero_undef ---

declare i32 @llvm.cttz.i32(i32, i1)

define i32 @cttz_undef(i32 %a) nounwind {
; CHECK-LABEL: cttz_undef:
; CHECK:       ffs $0, $32, {{.*}}, %r0
; CHECK-NEXT:  ret
  %r = call i32 @llvm.cttz.i32(i32 %a, i1 true)
  ret i32 %r
}

; cttz with defined-at-zero expands to FFS + zero check
define i32 @cttz_defined(i32 %a) nounwind {
; CHECK-LABEL: cttz_defined:
; CHECK:       tstl
; CHECK:       beql
; CHECK:       ffs $0, $32,
  %r = call i32 @llvm.cttz.i32(i32 %a, i1 false)
  ret i32 %r
}

;; --- FFS with memory operand ---

define i32 @cttz_from_mem(ptr %p) nounwind {
; CHECK-LABEL: cttz_from_mem:
; CHECK:       ffs $0, $32, (%r{{[0-9]+}}), %r0
; CHECK-NEXT:  ret
  %val = load i32, ptr %p
  %r = call i32 @llvm.cttz.i32(i32 %val, i1 true)
  ret i32 %r
}
