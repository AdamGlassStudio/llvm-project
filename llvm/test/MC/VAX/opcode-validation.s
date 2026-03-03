# RUN: llvm-mc -triple=vax-unknown-netbsdelf -show-encoding %s | FileCheck %s
#
# Validates that LLVM's MC encoder produces correct opcode bytes for all
# VAX instructions. Reference: VAX Architecture Reference Manual (1987),
# cross-checked against GAS (vax--netbsdelf-as) output.
#
# This test exists because opcode transcription errors were found in
# VAXInstrInfo.td (e.g., ADDL2 encoded as ADDW2, MOVF as MOVL, CVTFL
# as CVTFD). Every instruction defined in the backend should have an
# encoding check here.
#
# MAINTAINER NOTE: When adding a new instruction to VAXInstrInfo.td,
# add a corresponding encoding check here. To get the expected bytes,
# assemble the instruction with GAS and objdump:
#   echo "newinst %r0, %r1" | vax--netbsdelf-as -o /tmp/t.o -
#   vax--netbsdelf-objdump -d /tmp/t.o

# === Zero-operand instructions ===

halt
# CHECK: halt     # encoding: [0x00]

nop
# CHECK: nop      # encoding: [0x01]

ret
# CHECK: ret      # encoding: [0x04]

# === Longword integer arithmetic ===

movl %r0, %r1
# CHECK: movl %r0, %r1   # encoding: [0xd0,0x50,0x51]

movl $5, %r0
# CHECK: movl $5, %r0    # encoding: [0xd0,0x05,0x50]

addl2 %r0, %r1
# CHECK: addl2 %r0, %r1  # encoding: [0xc0,0x50,0x51]

addl2 $5, %r1
# CHECK: addl2 $5, %r1   # encoding: [0xc0,0x05,0x51]

addl3 %r0, %r1, %r2
# CHECK: addl3 %r0, %r1, %r2  # encoding: [0xc1,0x50,0x51,0x52]

subl2 %r0, %r1
# CHECK: subl2 %r0, %r1  # encoding: [0xc2,0x50,0x51]

subl2 $5, %r1
# CHECK: subl2 $5, %r1   # encoding: [0xc2,0x05,0x51]

subl3 %r0, %r1, %r2
# CHECK: subl3 %r0, %r1, %r2  # encoding: [0xc3,0x50,0x51,0x52]

mull2 %r0, %r1
# CHECK: mull2 %r0, %r1  # encoding: [0xc4,0x50,0x51]

bisl2 %r0, %r1
# CHECK: bisl2 %r0, %r1  # encoding: [0xc8,0x50,0x51]

bicl2 %r0, %r1
# CHECK: bicl2 %r0, %r1  # encoding: [0xca,0x50,0x51]

bicl3 %r0, %r1, %r2
# CHECK: bicl3 %r0, %r1, %r2  # encoding: [0xcb,0x50,0x51,0x52]

xorl2 %r0, %r1
# CHECK: xorl2 %r0, %r1  # encoding: [0xcc,0x50,0x51]

cmpl %r0, %r1
# CHECK: cmpl %r0, %r1   # encoding: [0xd1,0x50,0x51]

mcoml %r0, %r1
# CHECK: mcoml %r0, %r1  # encoding: [0xd2,0x50,0x51]

mnegl %r0, %r1
# CHECK: mnegl %r0, %r1  # encoding: [0xce,0x50,0x51]

clrl %r0
# CHECK: clrl %r0         # encoding: [0xd4,0x50]

tstl %r0
# CHECK: tstl %r0         # encoding: [0xd5,0x50]

incl %r0
# CHECK: incl %r0         # encoding: [0xd6,0x50]

decl %r0
# CHECK: decl %r0         # encoding: [0xd7,0x50]

pushl %r0
# CHECK: pushl %r0        # encoding: [0xdd,0x50]

pushl $5
# CHECK: pushl $5         # encoding: [0xdd,0x05]

# === Byte and word operations ===

movb %r0, %r1
# CHECK: movb %r0, %r1   # encoding: [0x90,0x50,0x51]

cvtbl %r0, %r1
# CHECK: cvtbl %r0, %r1  # encoding: [0x98,0x50,0x51]

movzbl %r0, %r1
# CHECK: movzbl %r0, %r1 # encoding: [0x9a,0x50,0x51]

movw %r0, %r1
# CHECK: movw %r0, %r1   # encoding: [0xb0,0x50,0x51]

cvtwl %r0, %r1
# CHECK: cvtwl %r0, %r1  # encoding: [0x32,0x50,0x51]

movzwl %r0, %r1
# CHECK: movzwl %r0, %r1 # encoding: [0x3c,0x50,0x51]

# === F_float (32-bit) ===

movf %r0, %r1
# CHECK: movf %r0, %r1   # encoding: [0x50,0x50,0x51]

addf3 %r0, %r1, %r2
# CHECK: addf3 %r0, %r1, %r2  # encoding: [0x41,0x50,0x51,0x52]

subf3 %r0, %r1, %r2
# CHECK: subf3 %r0, %r1, %r2  # encoding: [0x43,0x50,0x51,0x52]

mulf3 %r0, %r1, %r2
# CHECK: mulf3 %r0, %r1, %r2  # encoding: [0x45,0x50,0x51,0x52]

divf3 %r0, %r1, %r2
# CHECK: divf3 %r0, %r1, %r2  # encoding: [0x47,0x50,0x51,0x52]

cmpf %r0, %r1
# CHECK: cmpf %r0, %r1   # encoding: [0x51,0x50,0x51]

mnegf %r0, %r1
# CHECK: mnegf %r0, %r1  # encoding: [0x52,0x50,0x51]

tstf %r0
# CHECK: tstf %r0         # encoding: [0x53,0x50]

cvtfl %r0, %r1
# CHECK: cvtfl %r0, %r1  # encoding: [0x4a,0x50,0x51]

cvtlf %r0, %r1
# CHECK: cvtlf %r0, %r1  # encoding: [0x4e,0x50,0x51]

cvtfd %r0, %r1
# CHECK: cvtfd %r0, %r1  # encoding: [0x56,0x50,0x51]

# === D_float (64-bit) ===

movd %r0, %r1
# CHECK: movd %r0, %r1   # encoding: [0x70,0x50,0x51]

addd3 %r0, %r1, %r2
# CHECK: addd3 %r0, %r1, %r2  # encoding: [0x61,0x50,0x51,0x52]

subd3 %r0, %r1, %r2
# CHECK: subd3 %r0, %r1, %r2  # encoding: [0x63,0x50,0x51,0x52]

muld3 %r0, %r1, %r2
# CHECK: muld3 %r0, %r1, %r2  # encoding: [0x65,0x50,0x51,0x52]

divd3 %r0, %r1, %r2
# CHECK: divd3 %r0, %r1, %r2  # encoding: [0x67,0x50,0x51,0x52]

cmpd %r0, %r1
# CHECK: cmpd %r0, %r1   # encoding: [0x71,0x50,0x51]

mnegd %r0, %r1
# CHECK: mnegd %r0, %r1  # encoding: [0x72,0x50,0x51]

tstd %r0
# CHECK: tstd %r0         # encoding: [0x73,0x50]

cvtdl %r0, %r1
# CHECK: cvtdl %r0, %r1  # encoding: [0x6a,0x50,0x51]

cvtld %r0, %r1
# CHECK: cvtld %r0, %r1  # encoding: [0x6e,0x50,0x51]

cvtdf %r0, %r1
# CHECK: cvtdf %r0, %r1  # encoding: [0x76,0x50,0x51]

# === Shift and extended multiply/divide ===

ashl %r0, %r1, %r2
# CHECK: ashl %r0, %r1, %r2  # encoding: [0x78,0x50,0x51,0x52]

ashq %r0, %r1, %r2
# CHECK: ashq %r0, %r1, %r2  # encoding: [0x79,0x50,0x51,0x52]

rotl %r0, %r1, %r2
# CHECK: rotl %r0, %r1, %r2  # encoding: [0x9c,0x50,0x51,0x52]

# === Carry arithmetic ===

adwc %r0, %r1
# CHECK: adwc %r0, %r1   # encoding: [0xd8,0x50,0x51]

sbwc %r0, %r1
# CHECK: sbwc %r0, %r1   # encoding: [0xd9,0x50,0x51]

# === Addressing modes (using MOVL as the vehicle) ===
# Each check validates the operand specifier encoding, not just the opcode.

# Register direct: Rn → 5n
movl %r0, %r1
# CHECK: movl %r0, %r1  # encoding: [0xd0,0x50,0x51]

# Register deferred: (Rn) → 6n
movl (%r0), %r1
# CHECK: movl (%r0), %r1  # encoding: [0xd0,0x60,0x51]

# Byte displacement: disp(Rn) → An disp8
movl 4(%r0), %r1
# CHECK: movl 4(%r0), %r1  # encoding: [0xd0,0xa0,0x04,0x51]

# AP-relative displacement (R12=AP)
movl 4(%ap), %r1
# CHECK: movl 4(%ap), %r1  # encoding: [0xd0,0xac,0x04,0x51]

# FP-relative displacement (R13=FP)
movl 4(%fp), %r1
# CHECK: movl 4(%fp), %r1  # encoding: [0xd0,0xad,0x04,0x51]

# Short literal: $n (0-63) → literal byte
movl $42, %r0
# CHECK: movl $42, %r0  # encoding: [0xd0,0x2a,0x50]

# Autoincrement: (Rn)+ → 8n (SP=R14=0xE)
movl (%sp)+, %r0
# CHECK: movl (%sp)+, %r0  # encoding: [0xd0,0x8e,0x50]

# Autodecrement: -(Rn) → 7n
movl %r0, -(%sp)
# CHECK: movl %r0, -(%sp)  # encoding: [0xd0,0x50,0x7e]
