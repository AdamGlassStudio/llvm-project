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

#
# .double directive — emits VAX D_float (8 bytes, word-swapped)
#

# D_float 1.5: exp=129 (0x81), frac=0.11 → W0=0x40C0, W1-W3=0
# Emitted as two .long: 0x000040C0, 0x00000000
.double 1.5
# CHECK: .long 16576
# CHECK: .long 0

# D_float with 0d prefix
.double 0d1.5
# CHECK: .long 16576
# CHECK: .long 0

# D_float with 0d prefix and explicit sign
.double 0d+1.5
# CHECK: .long 16576
# CHECK: .long 0

# D_float negative
.double 0d-1.5
# CHECK: .long 49344
# CHECK: .long 0

# D_float zero
.double 0d0.0
# CHECK: .long 0
# CHECK: .long 0

# D_float scientific notation (from libm n_argred.S)
.double 0d-7.53080332264191085773e-13
# CHECK: .long 4180192339
# CHECK: .long 620767230

# D_float 1.0
.double 0d1.0
# CHECK: .long 16512
# CHECK: .long 0

#
# .float directive — emits VAX F_float (4 bytes, word-swapped)
#

# F_float 0.5: exp=128 (0x80), frac=0.1 → W0=0x4000
.float 0.5
# CHECK: .long 16384

# F_float with 0f prefix
.float 0f0.5
# CHECK: .long 16384

# F_float negative
.float 0f-0.7053061224
# CHECK: .long 2398208052

# F_float 1.0
.float 0f1.0
# CHECK: .long 16512

# F_float 3.0
.float 0f3.0
# CHECK: .long 16704
