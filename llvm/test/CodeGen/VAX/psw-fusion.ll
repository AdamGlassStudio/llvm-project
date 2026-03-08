; RUN: llc -mtriple=vax-unknown-netbsdelf < %s | FileCheck %s
;
; Regression tests for CMP+BCC fusion (VAXFixupPSW pass).
;
; The VAX is unusual: ALL instructions (including MOVL, CLRL, etc.) set the
; PSW condition codes.  The register allocator may insert copies between a
; compare/test and its conditional branch, clobbering CC.  The fusion pass
; combines CMP+BCC into a single pseudo to prevent this.
;
; Every CHECK-NEXT after a cmp/tst verifies no instruction is inserted
; between the compare and its branch — the core invariant.

; ---------------------------------------------------------------------------
; Memory-operand compare: cmpl (mem), reg
;
; This was the kernel boot hang in uvm_rb_insert: CMPL_rm was not fused,
; allowing a MOVL to clobber CC between compare and branch.
; ---------------------------------------------------------------------------

; Linked-list search: compare through pointer (CMPL_rm) and null check (TSTL).
; CHECK-LABEL: search_list:
; CHECK:       cmpl (%r1), %r0
; CHECK-NEXT:  beql
; CHECK:       movl 4(%r1), %r1
; CHECK-NEXT:  bneq
define i32 @search_list(ptr %node, i32 %key) {
entry:
  br label %loop

loop:
  %p = phi ptr [ %node, %entry ], [ %next_ptr, %loop_cont ]
  %val = load i32, ptr %p, align 4
  %cmp = icmp eq i32 %val, %key
  br i1 %cmp, label %found, label %loop_cont

loop_cont:
  %next_addr = getelementptr i32, ptr %p, i32 1
  %next_ptr = load ptr, ptr %next_addr, align 4
  %done = icmp eq ptr %next_ptr, null
  br i1 %done, label %not_found, label %loop

found:
  ret i32 1

not_found:
  ret i32 0
}

; Red-black tree comparison — the exact pattern from uvm_rb_insert.
; CHECK-LABEL: rb_compare:
; CHECK:       cmpl (%r1), %r0
; CHECK-NEXT:  bgeq
define i32 @rb_compare(ptr %tree_node, i32 %search_key) {
  %node_key = load i32, ptr %tree_node, align 4
  %cmp = icmp slt i32 %node_key, %search_key
  br i1 %cmp, label %go_right, label %go_left

go_right:
  ret i32 1

go_left:
  ret i32 -1
}

; ---------------------------------------------------------------------------
; Memory test-against-zero: tstl (mem)
; ---------------------------------------------------------------------------

; CHECK-LABEL: test_mem_zero:
; CHECK:       tstl (%r0)
; CHECK-NEXT:  beql
define i32 @test_mem_zero(ptr %p) {
  %val = load i32, ptr %p, align 4
  %cmp = icmp eq i32 %val, 0
  br i1 %cmp, label %is_zero, label %nonzero

is_zero:
  ret i32 1

nonzero:
  ret i32 0
}

; ---------------------------------------------------------------------------
; Register pressure: force RA to want to insert copies around CMP.
; ---------------------------------------------------------------------------

; CHECK-LABEL: regpressure_cmp:
; CHECK:       cmpl %r{{[0-9]+}}, %r{{[0-9]+}}
; CHECK-NEXT:  bleq
define i32 @regpressure_cmp(i32 %a, i32 %b, i32 %c, i32 %d,
                            i32 %e, i32 %f, i32 %g, i32 %h) {
  %sum1 = add i32 %a, %b
  %sum2 = add i32 %c, %d
  %sum3 = add i32 %e, %f
  %sum4 = add i32 %g, %h
  %cmp = icmp sgt i32 %a, %b
  br i1 %cmp, label %then, label %else

then:
  %r1 = add i32 %sum1, %sum2
  %r2 = add i32 %r1, %sum3
  %r3 = add i32 %r2, %sum4
  ret i32 %r3

else:
  %r4 = sub i32 %sum1, %sum2
  %r5 = sub i32 %r4, %sum3
  %r6 = sub i32 %r5, %sum4
  ret i32 %r6
}

; FP comparison under register pressure.
; CHECK-LABEL: regpressure_cmpf:
; CHECK:       cmpf %r{{[0-9]+}}, %r{{[0-9]+}}
; CHECK-NEXT:  bleq
define float @regpressure_cmpf(float %a, float %b, float %c, float %d,
                               float %e, float %f) {
  %sum1 = fadd float %a, %b
  %sum2 = fadd float %c, %d
  %sum3 = fadd float %e, %f
  %cmp = fcmp ogt float %a, %b
  br i1 %cmp, label %then, label %else

then:
  %r1 = fadd float %sum1, %sum2
  %r2 = fadd float %r1, %sum3
  ret float %r2

else:
  %r3 = fsub float %sum1, %sum2
  %r4 = fsub float %r3, %sum3
  ret float %r4
}

; ---------------------------------------------------------------------------
; FP compare-to-zero: tstf / tstd
;
; TSTF_BRANCH and TSTD_BRANCH were missing — caused FP SELECT_CC miscompile.
; ---------------------------------------------------------------------------

; CHECK-LABEL: tstf_branch:
; CHECK:       movf 4(%ap), %r0
; CHECK-NEXT:  bneq
define i32 @tstf_branch(float %x) {
  %cmp = fcmp oeq float %x, 0.0
  br i1 %cmp, label %zero, label %nonzero

zero:
  ret i32 1

nonzero:
  ret i32 0
}

; CHECK-LABEL: tstd_branch:
; CHECK:       movd 4(%ap), %r0
; CHECK-NEXT:  bneq
define i32 @tstd_branch(double %x) {
  %cmp = fcmp oeq double %x, 0.0
  br i1 %cmp, label %zero, label %nonzero

zero:
  ret i32 1

nonzero:
  ret i32 0
}

; TSTF with register pressure — ensure fusion holds even when RA needs copies.
; CHECK-LABEL: tstf_regpressure:
; CHECK:       tstf %r{{[0-9]+}}
; CHECK-NEXT:  bneq
define float @tstf_regpressure(float %a, float %b, float %c, float %d) {
  %sum1 = fadd float %a, %b
  %sum2 = fadd float %c, %d
  %cmp = fcmp oeq float %a, 0.0
  br i1 %cmp, label %then, label %else

then:
  %r1 = fadd float %sum1, %sum2
  ret float %r1

else:
  %r2 = fsub float %sum1, %sum2
  ret float %r2
}

; ---------------------------------------------------------------------------
; Narrow type promotion: i8/i16 comparisons use cmpl (i32), not cmpb/cmpw.
;
; When i8/i16 were legal types, DAGCombiner narrowed comparisons to byte
; width. VAX byte/word instructions don't zero upper bits, so comparisons
; of sign-extended values must happen at i32.
; ---------------------------------------------------------------------------

; i8 sign-extend then compare: must use cvtbl + cmpl, NOT cmpb.
; CHECK-LABEL: i8_upper_bits_matter:
; CHECK:       cvtbl
; CHECK:       cvtbl
; CHECK:       cmpl %r{{[0-9]+}}, %r{{[0-9]+}}
; CHECK-NEXT:  bneq
define i32 @i8_upper_bits_matter(i32 %a, i32 %b) {
  %a8 = trunc i32 %a to i8
  %b8 = trunc i32 %b to i8
  %a32 = sext i8 %a8 to i32
  %b32 = sext i8 %b8 to i32
  %cmp = icmp eq i32 %a32, %b32
  br i1 %cmp, label %equal, label %not_equal

equal:
  ret i32 1

not_equal:
  ret i32 0
}

; i16 sign-extend then compare: must use cvtwl + cmpl, NOT cmpw.
; CHECK-LABEL: i16_upper_bits_matter:
; CHECK:       cvtwl
; CHECK:       cvtwl
; CHECK:       cmpl %r{{[0-9]+}}, %r{{[0-9]+}}
; CHECK-NEXT:  bneq
define i32 @i16_upper_bits_matter(i32 %a, i32 %b) {
  %a16 = trunc i32 %a to i16
  %b16 = trunc i32 %b to i16
  %a32 = sext i16 %a16 to i32
  %b32 = sext i16 %b16 to i32
  %cmp = icmp eq i32 %a32, %b32
  br i1 %cmp, label %equal, label %not_equal

equal:
  ret i32 1

not_equal:
  ret i32 0
}
