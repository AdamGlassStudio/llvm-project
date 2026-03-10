# RUN: llvm-mc -triple=vax-unknown-netbsdelf -filetype=obj %s -o %t.o
# RUN: llvm-mc -triple=vax-unknown-netbsdelf -show-encoding %s | FileCheck %s
#
# Regression test for IAS kernel boot crash (March 2026):
# Symbol-based absolute/deferred operands (*sym, *sym[%reg]) must emit
# PC-relative deferred encoding (0xFF + R_VAX_PC32), not absolute mode
# (0x9F + R_VAX_32). Both are architecturally valid, but R_VAX_32
# relocations were not resolved correctly by the linker in all contexts,
# causing corrupted pointer dereferences through Sysmap at boot.
#
# The pattern comes from NetBSD's kvtopte/kvtophys inline asm:
#   moval *Sysmap[%reg],%reg
#   ashl  $9,*Sysmap[%reg],%reg

.text
.globl Sysmap

# Deferred symbol with indexed mode — the exact crashing pattern.
# Must emit 0xFF (PC-relative deferred), NOT 0x9F (absolute).
# The [%r2] index prefix is 0x42.
moval *Sysmap[%r2], %r2
# CHECK: moval {{.*}}[%r2], %r2 # encoding: [0xde,0x42,0xff,A,A,A,A,0x52]

# Deferred symbol without index — also must use 0xFF.
movl *Sysmap, %r0
# CHECK: movl {{.*}}, %r0 # encoding: [0xd0,0xff,A,A,A,A,0x50]

# ashl with deferred symbol + index (kvtophys pattern)
ashl $9, *Sysmap[%r0], %r0
# CHECK: ashl $9, {{.*}}[%r0], %r0 # encoding: [0x78,0x09,0x40,0xff,A,A,A,A,0x50]
