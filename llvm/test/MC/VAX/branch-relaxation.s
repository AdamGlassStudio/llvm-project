# RUN: llvm-mc -triple=vax-unknown-netbsdelf -filetype=obj %s -o %t.o
# RUN: od -A x -t x1 -N 3 %t.o --skip-bytes=52 | FileCheck %s
#
# Test BRB→BRW branch relaxation. When a BRB target is out of range
# (>127 bytes forward), the assembler must relax it to BRW during
# object emission.
#
# .text starts at ELF file offset 0x34 (52). The first instruction
# should be BRW (opcode 0x31) not BRB (opcode 0x11).

.text
	brb target
# CHECK: 000034 31

	# 130 bytes of padding to push target out of BRB range
	.space 130, 0x01

target:
	movl %r0, %r1
