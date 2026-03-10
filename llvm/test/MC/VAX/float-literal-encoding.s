# RUN: llvm-mc -triple=vax-unknown-netbsdelf -show-encoding %s | FileCheck %s
#
# Test GAS-style VAX float literal parsing: $0d<value> for D_float,
# $0f<value> for F_float. These appear in NetBSD .S files.
# The parser converts IEEE doubles/floats to VAX format.

# D_float zero: $0d0.0 → VAX D_float all-zeros → short literal $0
clrd $0d0.0
# CHECK: clrd $0  # encoding: [0x7c,0x00]

# F_float 1.0: IEEE 1.0 = 0x3F800000, VAX F_float 1.0 = 0x00004080
# Treated as longword immediate 16512 (0x4080)
movl $0f1.0, %r0
# CHECK: movl $16512, %r0  # encoding: [0xd0,0x8f,0x80,0x40,0x00,0x00,0x50]
