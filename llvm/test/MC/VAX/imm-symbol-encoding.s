# RUN: llvm-mc -triple=vax -show-encoding %s | FileCheck %s
# RUN: llvm-mc -triple=vax -filetype=obj %s -o %t.o
# RUN: llvm-readelf -r %t.o | FileCheck --check-prefix=RELOC %s

# Verify that $symbol (immediate) and symbol (displacement) produce
# different addressing modes and relocation types.
# GAS syntax: $sym = immediate mode (0x8F, R_VAX_32)
#             sym  = PC-relative displacement (0xCF/0xEF, R_VAX_PC32)

# --- pushl ---

# CHECK: pushl  bar@ABS  # encoding: [0xdd,0x8f,A,A,A,A]
# CHECK: # fixup A - offset: 2, value: bar@ABS, kind: FK_Data_4
pushl $bar

# CHECK: pushl  bar  # encoding: [0xdd,0xcf,A,A]
# CHECK: # fixup A - offset: 2, value: bar, kind: fixup_vax_pcrel_16
pushl bar

# --- movl ---

# CHECK: movl  bar@ABS, %r0  # encoding: [0xd0,0x8f,A,A,A,A,0x50]
# CHECK: # fixup A - offset: 2, value: bar@ABS, kind: FK_Data_4
movl $bar, %r0

# CHECK: movl  bar, %r0  # encoding: [0xd0,0xcf,A,A,0x50]
# CHECK: # fixup A - offset: 2, value: bar, kind: fixup_vax_pcrel_16
movl bar, %r0

# --- expression with addend ---

# CHECK: movl  bar@ABS+4, %r0  # encoding: [0xd0,0x8f,A,A,A,A,0x50]
# CHECK: # fixup A - offset: 2, value: bar@ABS+4, kind: FK_Data_4
movl $bar+4, %r0

# --- constant immediate unchanged ---

# CHECK: pushl  $42  # encoding: [0xdd,0x2a]
pushl $42

# Relocation types in object file:
# Type 1 = R_VAX_32 (absolute), Type 4 = R_VAX_PC32 (PC-relative)
# RELOC: 00000002 00000101{{.*}}bar + 0
# RELOC: 00000008 {{.*}}04{{.*}}bar + 0
# RELOC: 0000000e 00000101{{.*}}bar + 0
# RELOC: 00000015 {{.*}}04{{.*}}bar + 0
# RELOC: 0000001c 00000101{{.*}}bar + 4
