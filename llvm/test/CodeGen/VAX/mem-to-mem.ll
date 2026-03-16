; RUN: llc -mtriple=vax-unknown-netbsdelf < %s | FileCheck %s

; Test memory-to-memory and register-mediated copy patterns.
; i32/f32/f64 use mem-to-mem instructions directly.
; i8/i16 go through registers with legal byte/word types.

;; ---- i32 mem-to-mem ----

; Simple pointer-to-pointer copy: *dst = *src
define void @copy_i32(ptr %src, ptr %dst) nounwind {
; CHECK-LABEL: copy_i32:
; CHECK:       movl (%r{{[0-9]+}}), (%r{{[0-9]+}})
; CHECK-NEXT:  ret
  %v = load i32, ptr %src
  store i32 %v, ptr %dst
  ret void
}

;; ---- i8 copy ----

; Byte copy: *dst = *src (i8)
define void @copy_i8(ptr %src, ptr %dst) nounwind {
; CHECK-LABEL: copy_i8:
; CHECK:       movb (%r{{[0-9]+}}), %r{{[0-9]+}}
; CHECK:       movb %r{{[0-9]+}}, (%r{{[0-9]+}})
; CHECK:       ret
  %v = load i8, ptr %src
  store i8 %v, ptr %dst
  ret void
}

;; ---- i16 copy ----

; Word copy: *dst = *src (i16)
define void @copy_i16(ptr %src, ptr %dst) nounwind {
; CHECK-LABEL: copy_i16:
; CHECK:       movw (%r{{[0-9]+}}), %r{{[0-9]+}}
; CHECK:       movw %r{{[0-9]+}}, (%r{{[0-9]+}})
; CHECK:       ret
  %v = load i16, ptr %src
  store i16 %v, ptr %dst
  ret void
}

;; ---- f32 mem-to-mem ----

; F_float copy: *dst = *src (float)
define void @copy_f32(ptr %src, ptr %dst) nounwind {
; CHECK-LABEL: copy_f32:
; CHECK:       movf (%r{{[0-9]+}}), (%r{{[0-9]+}})
; CHECK-NEXT:  ret
  %v = load float, ptr %src
  store float %v, ptr %dst
  ret void
}

;; ---- f64 mem-to-mem ----

; D_float copy: *dst = *src (double)
define void @copy_f64(ptr %src, ptr %dst) nounwind {
; CHECK-LABEL: copy_f64:
; CHECK:       movd (%r{{[0-9]+}}), (%r{{[0-9]+}})
; CHECK-NEXT:  ret
  %v = load double, ptr %src
  store double %v, ptr %dst
  ret void
}

;; ---- Negative: load with multiple uses should NOT fold ----

; When the loaded value is used both for a store and a return,
; the load must go through a register (cannot fold into _mm).
define i32 @no_fold_multi_use(ptr %src, ptr %dst) nounwind {
; CHECK-LABEL: no_fold_multi_use:
; CHECK:       movl (%r{{[0-9]+}}), %r0
; CHECK:       movl %r0, (%r{{[0-9]+}})
; CHECK:       ret
  %v = load i32, ptr %src
  store i32 %v, ptr %dst
  ret i32 %v
}
