; RUN: llc -march=vax < %s | FileCheck %s

; Test that blockaddress (computed goto) is correctly lowered.
; The regex engine in diffutils uses computed goto via GCC's &&label extension.

; Indirect table-based computed goto
define i32 @computed_goto(i32 %idx) {
; CHECK-LABEL: computed_goto:
; CHECK: moval	targets, %r1
; CHECK: jmp	(%r0)
entry:
  %addr = getelementptr inbounds [2 x ptr], ptr @targets, i32 0, i32 %idx
  %target = load ptr, ptr %addr
  indirectbr ptr %target, [label %label_a, label %label_b]

label_a:
  ret i32 10

label_b:
  ret i32 20
}

@targets = internal constant [2 x ptr] [ptr blockaddress(@computed_goto, %label_a),
                                         ptr blockaddress(@computed_goto, %label_b)]

; Direct blockaddress materialization (exercises MOVAL_ba pattern)
define i32 @computed_goto_direct() {
; CHECK-LABEL: computed_goto_direct:
; CHECK: moval	.Ltmp{{[0-9]+}}, %r0
; CHECK: jmp	(%r0)
entry:
  %addr = select i1 true, ptr blockaddress(@computed_goto_direct, %label_a),
                          ptr blockaddress(@computed_goto_direct, %label_b)
  indirectbr ptr %addr, [label %label_a, label %label_b]

label_a:
  ret i32 10

label_b:
  ret i32 20
}
