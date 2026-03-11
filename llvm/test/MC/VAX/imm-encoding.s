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

# ---- Sub-longword immediate operand widths ----
# Instructions with .rw (word) source operands must emit 2-byte immediates.
# Instructions with .rb (byte) source operands must emit 1-byte immediates.

# movzwl (.rw source): large immediate → 0x8F + 2 bytes
movzwl $0xffff, %r0
# CHECK: movzwl $65535, %r0  # encoding: [0x3c,0x8f,0xff,0xff,0x50]

movzwl $0x1234, %r0
# CHECK: movzwl $4660, %r0  # encoding: [0x3c,0x8f,0x34,0x12,0x50]

# movzwl short literal (0-63): single byte, no width issue
movzwl $42, %r0
# CHECK: movzwl $42, %r0  # encoding: [0x3c,0x2a,0x50]

# cvtwl (.rw source): large immediate → 0x8F + 2 bytes
cvtwl $0x1234, %r0
# CHECK: cvtwl $4660, %r0  # encoding: [0x32,0x8f,0x34,0x12,0x50]

# movzbl (.rb source): large immediate → 0x8F + 1 byte
movzbl $0xff, %r0
# CHECK: movzbl $255, %r0  # encoding: [0x9a,0x8f,0xff,0x50]

movzbl $0x80, %r0
# CHECK: movzbl $128, %r0  # encoding: [0x9a,0x8f,0x80,0x50]

# cvtbl (.rb source): large immediate → 0x8F + 1 byte
cvtbl $0x80, %r0
# CHECK: cvtbl $128, %r0  # encoding: [0x98,0x8f,0x80,0x50]
