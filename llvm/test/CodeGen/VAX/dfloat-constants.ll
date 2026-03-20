; RUN: llc -mtriple=vax-unknown-netbsdelf < %s | FileCheck %s

; Test that D_float constant bit patterns survive LLVM optimization and are
; emitted correctly.  Clang's constant folder must not misinterpret D_float
; values as IEEE 754 and zero them out.

@d_one = dso_local global double 1.0, align 4
@d_zero = dso_local global double 0.0, align 4

; --- Functions returning constants ---

; D_float 1.0 constant pool entry must be non-zero (16512 = 0x4080).
; CHECK-LABEL: return_const_one:
; CHECK:       movd {{.*}}, %r0
define double @return_const_one() nounwind {
  ret double 1.0
}

; CHECK-LABEL: return_const_zero:
; CHECK:       movd {{.*}}, %r0
define double @return_const_zero() nounwind {
  ret double 0.0
}

; CHECK-LABEL: load_d_one:
; CHECK:       movd d_one, %r0
define double @load_d_one() nounwind {
  %v = load double, ptr @d_one, align 4
  ret double %v
}

; --- Global data section ---
; D_float 1.0 = 0x0000000000004080 → first .long must be 16512, not zero.
; CHECK-LABEL: d_one:
; CHECK:       .long 16512
; CHECK-NEXT:  .long 0

; D_float 0.0 → both halves zero.
; CHECK-LABEL: d_zero:
; CHECK:       .long 0
; CHECK-NEXT:  .long 0
