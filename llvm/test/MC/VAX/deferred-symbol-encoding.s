# RUN: llvm-mc -triple=vax-unknown-netbsdelf -filetype=obj %s -o %t.o
# RUN: llvm-mc -triple=vax-unknown-netbsdelf -show-encoding %s | FileCheck %s
#
# Regression test for IAS kernel boot crash (March 2026):
# Symbol-based absolute/deferred operands (*sym, *sym[%reg]) must emit
# PC-relative deferred encoding, not absolute mode (0x9F + R_VAX_32).
# Both are architecturally valid, but R_VAX_32 relocations were not
# resolved correctly by the linker in all contexts, causing corrupted
# pointer dereferences through Sysmap at boot.
#
# Initial encoding uses word displacement deferred (0xDF + 2-byte),
# with relaxation growing to longword (0xFF + 4-byte) when needed.
#
# The pattern comes from NetBSD's kvtopte/kvtophys inline asm:
#   moval *Sysmap[%reg],%reg
#   ashl  $9,*Sysmap[%reg],%reg

.text
.globl Sysmap

# Deferred symbol with indexed mode — the exact crashing pattern.
# Must emit 0xDF (word PC-relative deferred), NOT 0x9F (absolute).
# The [%r2] index prefix is 0x42.
moval *Sysmap[%r2], %r2
# CHECK: moval {{.*}}[%r2], %r2 # encoding: [0xde,0x42,0xdf,A,A,0x52]

# Deferred symbol without index — also must use 0xDF.
movl *Sysmap, %r0
# CHECK: movl {{.*}}, %r0 # encoding: [0xd0,0xdf,A,A,0x50]

# ashl with deferred symbol + index (kvtophys pattern)
ashl $9, *Sysmap[%r0], %r0
# CHECK: ashl $9, {{.*}}[%r0], %r0 # encoding: [0x78,0x09,0x40,0xdf,A,A,0x50]
