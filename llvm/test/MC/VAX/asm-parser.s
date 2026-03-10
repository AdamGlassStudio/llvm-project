# RUN: llvm-mc -triple=vax-unknown-netbsdelf -show-encoding %s | FileCheck %s

# Basic instructions (zero operands)
# CHECK: nop{{.*}}encoding: [0x01]
	nop
# CHECK: ret{{.*}}encoding: [0x04]
	ret

# Register operands
# CHECK: clrl	%r0{{.*}}encoding: [0xd4,0x50]
	clrl %r0
# CHECK: movl	%r1, %r2{{.*}}encoding: [0xd0,0x51,0x52]
	movl %r1, %r2
# CHECK: subl3	%r0, %r1, %r2{{.*}}encoding: [0xc3,0x50,0x51,0x52]
	subl3 %r0, %r1, %r2

# Immediate operands (short literal 0-63)
# CHECK: movl	$42, %r0{{.*}}encoding: [0xd0,0x2a,0x50]
	movl $42, %r0
# CHECK: movl	$0, %r0{{.*}}encoding: [0xd0,0x00,0x50]
	movl $0, %r0
# CHECK: pushl	$0{{.*}}encoding: [0xdd,0x00]
	pushl $0

# Memory operands (byte displacement)
# CHECK: movl	4(%ap), %r0{{.*}}encoding: [0xd0,0xac,0x04,0x50]
	movl 4(%ap), %r0
# CHECK: movl	%r1, 8(%fp){{.*}}encoding: [0xd0,0x51,0xad,0x08]
	movl %r1, 8(%fp)
# CHECK: movl	12(%sp), %r3{{.*}}encoding: [0xd0,0xae,0x0c,0x53]
	movl 12(%sp), %r3

# Register deferred
# CHECK: calls	$1, (%r0){{.*}}encoding: [0xfb,0x01,0x60]
	calls $1, (%r0)

# Branches (fixup labels)
# CHECK: beql	.Ltmp0{{.*}}encoding: [0x13,A]
	beql 1f
# CHECK: bneq	.Ltmp0{{.*}}encoding: [0x12,A]
	bneq 1f
# CHECK: brb	.Ltmp0{{.*}}encoding: [0x11,A]
	brb 1f
1:

# FP instructions (GPR->QPR promotion)
# CHECK: tstd	%r0{{.*}}encoding: [0x73,0x50]
	tstd %r0
# CHECK: movd	%r0, %r2{{.*}}encoding: [0x70,0x50,0x52]
	movd %r0, %r2

# .word directive
# CHECK: .word 0
	.word 0

# External symbol (PC-relative, word displacement — relaxation grows to longword)
# CHECK: calls	$0, foo{{.*}}encoding: [0xfb,0x00,0xcf,A,A]
	calls $0, foo
# CHECK: movl	foo, %r0{{.*}}encoding: [0xd0,0xcf,A,A,0x50]
	movl foo, %r0
