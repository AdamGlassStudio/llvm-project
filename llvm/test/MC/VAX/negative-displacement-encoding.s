# RUN: llvm-mc -triple=vax-unknown-netbsdelf -show-encoding %s | FileCheck %s
#
# Regression test: negative displacements must fold to byte/word encoding.
# Before the evaluateAsAbsolute() fix, expressions like -20 were parsed as
# MCUnaryExpr(Minus, MCConstantExpr(20)) which didn't match the
# dyn_cast<MCConstantExpr> check in addExprOperand, causing all negative
# displacements to emit longword (0xEE/0xAE, 5 bytes) instead of
# byte (0xAE, 2 bytes). This added ~8KB bloat per kernel build.

# Byte displacement: -128 to +127 → 0xAn (byte disp, 2 bytes total)
# FP=R13→0xAD, AP=R12→0xAC, R0→0xA0, R5→0xA5
movl -20(%fp), %r0
# CHECK: movl -20(%fp), %r0  # encoding: [0xd0,0xad,0xec,0x50]

movl -1(%r0), %r1
# CHECK: movl -1(%r0), %r1  # encoding: [0xd0,0xa0,0xff,0x51]

movl -128(%ap), %r2
# CHECK: movl -128(%ap), %r2  # encoding: [0xd0,0xac,0x80,0x52]

movl 127(%fp), %r3
# CHECK: movl 127(%fp), %r3  # encoding: [0xd0,0xad,0x7f,0x53]

# Word displacement: -32768 to +32767 → 0xCn (word disp, 3 bytes total)
movl -200(%fp), %r0
# CHECK: movl -200(%fp), %r0  # encoding: [0xd0,0xcd,0x38,0xff,0x50]

movl 200(%fp), %r0
# CHECK: movl 200(%fp), %r0  # encoding: [0xd0,0xcd,0xc8,0x00,0x50]

movl -32768(%r5), %r1
# CHECK: movl -32768(%r5), %r1  # encoding: [0xd0,0xc5,0x00,0x80,0x51]

# Longword displacement: outside word range → 0xEn (longword disp, 5 bytes)
movl -32769(%fp), %r0
# CHECK: movl -32769(%fp), %r0  # encoding: [0xd0,0xed,0xff,0x7f,0xff,0xff,0x50]

movl 32768(%fp), %r0
# CHECK: movl 32768(%fp), %r0  # encoding: [0xd0,0xed,0x00,0x80,0x00,0x00,0x50]

# Positive byte displacement (sanity check)
movl 4(%ap), %r0
# CHECK: movl 4(%ap), %r0  # encoding: [0xd0,0xac,0x04,0x50]

# Zero displacement → register deferred (0x6n, 1 byte)
movl (%r3), %r0
# CHECK: movl (%r3), %r0  # encoding: [0xd0,0x63,0x50]

# Store direction: negative displacement on destination
movl %r0, -8(%fp)
# CHECK: movl %r0, -8(%fp)  # encoding: [0xd0,0x50,0xad,0xf8]
