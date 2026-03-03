; RUN: llc -mtriple=vax-unknown-netbsdelf < %s | FileCheck %s
;
; Test FP bitcast (f32↔i32, f64↔i64) expands through stack.
; VAX D_float is not IEEE754, so bitcasts must go through memory.

define i32 @bitcast_f32_to_i32(float %a) {
; CHECK-LABEL: bitcast_f32_to_i32:
; CHECK: movl {{.*}}(%ap), %r0
; CHECK: ret
  %r = bitcast float %a to i32
  ret i32 %r
}

define float @bitcast_i32_to_f32(i32 %a) {
; CHECK-LABEL: bitcast_i32_to_f32:
; CHECK: movf {{.*}}(%ap), %r0
; CHECK: ret
  %r = bitcast i32 %a to float
  ret float %r
}
