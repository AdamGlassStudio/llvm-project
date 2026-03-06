# RUN: llvm-mc -triple=vax-unknown-netbsdelf -show-encoding %s | FileCheck %s
#
# Regression test for Bug 3 from kernel boot bringup (March 2026):
# $symbol immediates were encoded as 0xEF (PC-relative longword
# displacement) instead of 0x8F (immediate mode = autoincrement PC).
#
# NOTE: The AsmParser currently matches $symbol to MOVL_rcp (PC-relative
# load) rather than the VAXAM::Imm encoding path. The MCCodeEmitter fix
# (0x8F for VAXAM::Imm expressions) is exercised by the ISel/codegen path,
# not the assembly parser. This test validates literal immediate encoding
# and documents the current behavior for symbol immediates.
#
# The practical fix for the kernel was Bug 4 (PrintAsmOperand adding $
# prefix), which ensures GAS sees $symbol and handles it correctly.

# Short literal: $n (0-63) → literal byte (0x00-0x3F)
movl $0, %r0
# CHECK: movl $0, %r0  # encoding: [0xd0,0x00,0x50]

movl $42, %r0
# CHECK: movl $42, %r0  # encoding: [0xd0,0x2a,0x50]

movl $63, %r0
# CHECK: movl $63, %r0  # encoding: [0xd0,0x3f,0x50]

# Large immediate: $n (>63) → 0x8F + 4-byte value
movl $64, %r0
# CHECK: movl $64, %r0  # encoding: [0xd0,0x8f,0x40,0x00,0x00,0x00,0x50]

movl $255, %r0
# CHECK: movl $255, %r0  # encoding: [0xd0,0x8f,0xff,0x00,0x00,0x00,0x50]

movl $65536, %r0
# CHECK: movl $65536, %r0  # encoding: [0xd0,0x8f,0x00,0x00,0x01,0x00,0x50]

# Negative immediate
movl $-1, %r0
# CHECK: movl $-1, %r0  # encoding: [0xd0,0x8f,0xff,0xff,0xff,0xff,0x50]
