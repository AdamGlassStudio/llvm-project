# RUN: llvm-mc -triple=vax-unknown-netbsdelf %s | FileCheck %s
# Phase 35: MC-layer completeness — assembler coverage for all new instructions.

# --- Branch-on-bit ---
# CHECK: bbs
bbs $1, %r0, .+4
# CHECK: bbc
bbc $0, (%r1), .+8
# CHECK: bbss
bbss $3, %r2, .+4
# CHECK: bbcs
bbcs $2, %r3, .+4
# CHECK: bbsc
bbsc $1, %r4, .+4
# CHECK: bbcc
bbcc $0, %r5, .+4
# CHECK: bbssi
bbssi $3, %r6, .+4
# CHECK: bbcci
bbcci $7, %r7, .+4
# CHECK: blbs
blbs %r0, .+4
# CHECK: blbc
blbc %r1, .+4

# --- Bit test (no branch) ---
# CHECK: bitl
bitl %r0, %r1
# CHECK: bitw
bitw %r2, %r3
# CHECK: bitb
bitb %r4, %r5

# --- Loop ---
# CHECK: sobgeq
sobgeq %r0, .+4
# CHECK: aobleq
aobleq %r1, %r2, .+4
# CHECK: aoblss
aoblss %r3, %r4, .+4
# CHECK: acbl
acbl %r0, %r1, %r2, .+4
# CHECK: acbw
acbw %r0, %r1, %r2, .+4
# CHECK: acbb
acbb %r0, %r1, %r2, .+4
# CHECK: acbf
acbf %r0, %r1, %r2, .+4
# CHECK: acbd
acbd %r0, %r1, %r2, .+4

# --- Case ---
# CHECK: caseb
caseb %r0, %r1, %r2
# CHECK: casew
casew %r3, %r4, %r5

# --- Subroutine ---
# CHECK: callg
callg (%r0), (%r1)
# CHECK: bsbb
bsbb .+4
# CHECK: bsbw
bsbw .+100
# CHECK: rsb
rsb

# --- Stack save/restore ---
# CHECK: pushr
pushr $0x3f
# CHECK: popr
popr $0x3f

# --- String ---
# CHECK: cmpc3
cmpc3 %r0, (%r1), (%r2)
# CHECK: cmpc5
cmpc5 %r0, (%r1), $0, %r2, (%r3)
# CHECK: movtc
movtc %r0, (%r1), $0, (%r2), %r3, (%r4)
# CHECK: movtuc
movtuc %r0, (%r1), $0, (%r2), %r3, (%r4)
# CHECK: crc
crc (%r0), %r1, %r2, (%r3)
# CHECK: matchc
matchc %r0, (%r1), %r2, (%r3)
# CHECK: spanc
spanc %r0, (%r1), (%r2), %r3

# --- Privileged / system ---
# CHECK: chmk
chmk $1
# CHECK: chme
chme $2
# CHECK: chms
chms $3
# CHECK: chmu
chmu $4
# CHECK: prober
prober $0, %r0, (%r1)
# CHECK: probew
probew $1, %r1, (%r2)
# CHECK: bpt
bpt
# CHECK: ldpctx
ldpctx
# CHECK: svpctx
svpctx
# CHECK: xfc
xfc

# --- Misc ---
# CHECK: adawi
adawi $1, (%r0)
# CHECK: insqhi
insqhi (%r0), (%r1)
# CHECK: insqti
insqti (%r0), (%r1)
# CHECK: remqhi
remqhi (%r0), (%r1)
# CHECK: remqti
remqti (%r0), (%r1)
# CHECK: index
index %r0, %r1, %r2, %r3, %r4, %r5

# --- J-form aliases for branch-on-bit ---
# CHECK: bbs
jbs $1, %r0, .+4
# CHECK: bbc
jbc $0, %r1, .+4
# CHECK: blbs
jlbs %r0, .+4
# CHECK: blbc
jlbc %r1, .+4
# CHECK: bbssi
jbssi $3, %r2, .+4
# CHECK: bbcci
jbcci $7, %r3, .+4
