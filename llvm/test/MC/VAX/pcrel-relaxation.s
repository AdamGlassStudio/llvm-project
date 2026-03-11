# RUN: llvm-mc -triple=vax-unknown-netbsdelf -filetype=obj %s -o %t.o
# RUN: od -A x -t x1 -j 0x34 -N 11 %t.o | FileCheck %s --check-prefix=NEARBY
# RUN: llvm-objdump -d %t.o | FileCheck %s --check-prefix=FAR
#
# Test PC-relative operand relaxation: nearby symbols use word displacement
# (0xCF/0xDF, 3 bytes) and far symbols relax to longword (0xEF/0xFF, 5 bytes).

.text

# --- Nearby symbols: should use word PC-relative ---
nearby:
	nop
# Word PC-relative (0xCF) for nearby non-deferred reference.
	movl nearby, %r0
# Word PC-relative deferred (0xDF) for nearby deferred reference.
	movl *nearby, %r0

# Bytes at .text start (offset 0x34 in file):
#   01                    nop
#   d0 cf fb ff 50        movl nearby, %r0  (0xCF = word pcrel, disp=-5)
#   d0 df f6 ff 50        movl *nearby, %r0 (0xDF = word pcrel deferred, disp=-10)
# NEARBY: 000034 01 d0 cf fb ff 50 d0 df f6 ff 50

# --- Far symbol: push target more than 32KB away ---
	.space 40000
far_target:
	nop
	.space 40000

# Longword PC-relative (0xEF) — far reference forces relaxation.
	movl far_target, %r0
# FAR: movl	{{-?[0-9]+}}(%pc), %r0

# Longword PC-relative deferred (0xFF) — far deferred also relaxes.
	movl *far_target, %r0
# FAR: movl	*{{-?[0-9]+}}(%pc), %r0

# --- External symbol: always relaxes to longword ---
	.globl external_func
	calls $0, external_func

# --- PLT always longword ---
	calls $0, external_func@PLT
