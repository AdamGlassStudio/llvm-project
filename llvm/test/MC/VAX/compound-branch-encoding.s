# RUN: llvm-mc -triple=vax-unknown-netbsdelf -show-encoding %s | FileCheck %s
#
# Validates that compound branch instructions (SOBxxx, AOBxxx, ACBx, BBxx,
# BLBx) encode their trailing branch displacement as a raw signed byte or
# word — NOT as a VAX operand specifier.
#
# Bug: Prior to this fix, the branch displacement was encoded as an operand
# specifier (0x8F immediate mode + 4-byte address = 5 bytes), making the
# instruction too long and branching to wrong addresses.
#
# Reference: VAX Architecture Reference Manual, instruction format:
#   .bb = byte branch displacement (1 byte, signed)
#   .bw = word branch displacement (2 bytes, signed)

# === Byte displacement (.bb) instructions ===

# SOBGTR (0xF5): index.ml, disp.bb
sobgtr %r0, .
# CHECK: sobgtr %r0,
# CHECK-SAME: encoding: [0xf5,0x50,A]
# CHECK: fixup_vax_pcrel_8

# SOBGEQ (0xF4): index.ml, disp.bb
sobgeq %r1, .
# CHECK: sobgeq %r1,
# CHECK-SAME: encoding: [0xf4,0x51,A]
# CHECK: fixup_vax_pcrel_8

# AOBLEQ (0xF3): limit.rl, index.ml, disp.bb
aobleq %r2, %r3, .
# CHECK: aobleq %r2, %r3,
# CHECK-SAME: encoding: [0xf3,0x52,0x53,A]
# CHECK: fixup_vax_pcrel_8

# AOBLSS (0xF2): limit.rl, index.ml, disp.bb
aoblss %r4, %r5, .
# CHECK: aoblss %r4, %r5,
# CHECK-SAME: encoding: [0xf2,0x54,0x55,A]
# CHECK: fixup_vax_pcrel_8

# BBS (0xE0): pos.rl, base.rl, disp.bb
bbs $0, %r0, .
# CHECK: bbs $0, %r0,
# CHECK-SAME: encoding: [0xe0,0x00,0x50,A]
# CHECK: fixup_vax_pcrel_8

# BBC (0xE1): pos.rl, base.rl, disp.bb
bbc $1, %r1, .
# CHECK: bbc $1, %r1,
# CHECK-SAME: encoding: [0xe1,0x01,0x51,A]
# CHECK: fixup_vax_pcrel_8

# BBSS (0xE2): pos.rl, base.rl, disp.bb
bbss $2, %r2, .
# CHECK: bbss $2, %r2,
# CHECK-SAME: encoding: [0xe2,0x02,0x52,A]
# CHECK: fixup_vax_pcrel_8

# BBCS (0xE3): pos.rl, base.rl, disp.bb
bbcs $3, %r3, .
# CHECK: bbcs $3, %r3,
# CHECK-SAME: encoding: [0xe3,0x03,0x53,A]
# CHECK: fixup_vax_pcrel_8

# BBSC (0xE4): pos.rl, base.rl, disp.bb
bbsc $4, %r4, .
# CHECK: bbsc $4, %r4,
# CHECK-SAME: encoding: [0xe4,0x04,0x54,A]
# CHECK: fixup_vax_pcrel_8

# BBCC (0xE5): pos.rl, base.rl, disp.bb
bbcc $5, %r5, .
# CHECK: bbcc $5, %r5,
# CHECK-SAME: encoding: [0xe5,0x05,0x55,A]
# CHECK: fixup_vax_pcrel_8

# BBSSI (0xE6): pos.rl, base.rl, disp.bb
bbssi $6, %r6, .
# CHECK: bbssi $6, %r6,
# CHECK-SAME: encoding: [0xe6,0x06,0x56,A]
# CHECK: fixup_vax_pcrel_8

# BBCCI (0xE7): pos.rl, base.rl, disp.bb
bbcci $7, %r7, .
# CHECK: bbcci $7, %r7,
# CHECK-SAME: encoding: [0xe7,0x07,0x57,A]
# CHECK: fixup_vax_pcrel_8

# BLBS (0xE8): src.rl, disp.bb
blbs %r0, .
# CHECK: blbs %r0,
# CHECK-SAME: encoding: [0xe8,0x50,A]
# CHECK: fixup_vax_pcrel_8

# BLBC (0xE9): src.rl, disp.bb
blbc %r1, .
# CHECK: blbc %r1,
# CHECK-SAME: encoding: [0xe9,0x51,A]
# CHECK: fixup_vax_pcrel_8

# === Word displacement (.bw) instructions ===

# ACBL (0xF1): limit.rl, add.rl, index.ml, disp.bw
acbl %r0, %r1, %r2, .
# CHECK: acbl %r0, %r1, %r2,
# CHECK-SAME: encoding: [0xf1,0x50,0x51,0x52,A,A]
# CHECK: fixup_vax_pcrel_16

# ACBW (0x3D): limit.rw, add.rw, index.mw, disp.bw
acbw %r0, %r1, %r2, .
# CHECK: acbw %r0, %r1, %r2,
# CHECK-SAME: encoding: [0x3d,0x50,0x51,0x52,A,A]
# CHECK: fixup_vax_pcrel_16

# ACBB (0x9D): limit.rb, add.rb, index.mb, disp.bw
acbb %r0, %r1, %r2, .
# CHECK: acbb %r0, %r1, %r2,
# CHECK-SAME: encoding: [0x9d,0x50,0x51,0x52,A,A]
# CHECK: fixup_vax_pcrel_16

# ACBF (0x4F): limit.rf, add.rf, index.mf, disp.bw
acbf %r0, %r1, %r2, .
# CHECK: acbf %r0, %r1, %r2,
# CHECK-SAME: encoding: [0x4f,0x50,0x51,0x52,A,A]
# CHECK: fixup_vax_pcrel_16

# ACBD (0x6F): limit.rd, add.rd, index.md, disp.bw
acbd %r0, %r1, %r2, .
# CHECK: acbd %r0, %r1, %r2,
# CHECK-SAME: encoding: [0x6f,0x50,0x51,0x52,A,A]
# CHECK: fixup_vax_pcrel_16
