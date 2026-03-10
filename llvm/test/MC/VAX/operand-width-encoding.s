# RUN: llvm-mc --triple=vax-unknown-netbsdelf --show-encoding %s | FileCheck %s

# Test that byte/word-width operands in immediate mode (0x8F) emit the
# correct number of data bytes. The encoder previously hardcoded DataSize=4
# for all VAXAM::Imm operands, but VAX instructions have per-operand
# widths: .rb=1 byte, .rw=2 bytes, .rl=4 bytes.

# movc5: srclen.rw, srcaddr.ab, fill.rb, dstlen.rw, dstaddr.ab
# dstlen=$4096 as word immediate: 8F 00 10 (3 bytes, not 5)
# CHECK: movc5 {{.*}} encoding: [0x2c,0x00,0x60,0x00,0x8f,0x00,0x10,0x60]
movc5 $0, (%r0), $0, $4096, (%r0)

# movc5 with fill=$255 as byte immediate: 8F FF (2 bytes, not 5)
# CHECK: movc5 {{.*}} encoding: [0x2c,0x00,0x60,0x8f,0xff,0x8f,0x64,0x00,0x60]
movc5 $0, (%r0), $255, $100, (%r0)

# movc3: len.rw — word immediate for $4096
# CHECK: movc3 {{.*}} encoding: [0x28,0x8f,0x00,0x10,0x60,0x61]
movc3 $4096, (%r0), (%r1)

# chmk: code.rw — word immediate
# CHECK: chmk {{.*}} encoding: [0xbc,0x8f,0x64,0x00]
chmk $100

# pushr: mask.rw — word immediate
# CHECK: pushr {{.*}} encoding: [0xbb,0x8f,0xff,0x01]
pushr $0x1ff

# popr: mask.rw — word immediate
# CHECK: popr {{.*}} encoding: [0xba,0x8f,0xff,0x01]
popr $0x1ff

# casew: all .rw operands (short literals here, still correct)
# CHECK: casew {{.*}} encoding: [0xaf,0x01,0x00,0x02]
casew $1, $0, $2

# adawi: src.rw, dst.mw — word immediate
# CHECK: adawi {{.*}} encoding: [0x58,0x8f,0x64,0x00,0x60]
adawi $100, (%r0)

# scanc: len.rw, addr.ab, tbladdr.ab, mask.rb (all short literals here)
# CHECK: scanc {{.*}} encoding: [0x2a,0x0a,0x60,0x61,0x20]
scanc $10, (%r0), (%r1), $32

# locc: char.rb, len.rw, addr.ab — len is word immediate
# CHECK: locc {{.*}} encoding: [0x3a,0x00,0x8f,0x64,0x00,0x60]
locc $0, $100, (%r0)

# movl: all .rl operands — longword (4 bytes) is correct
# CHECK: movl {{.*}} encoding: [0xd0,0x8f,0x00,0x01,0x00,0x00,0x50]
movl $256, %r0
