# RUN: llvm-mc -triple=vax-unknown-netbsdelf -filetype=obj %s -o %t.o
# RUN: llvm-objdump -d %t.o | FileCheck %s
#
# Test conditional branch relaxation. When a Bcc target is out of 8-bit
# range (±127 bytes), the assembler must expand it to an inverted Bcc + BRW:
#
#   beql far_target    →    bneq .+3
#                           brw far_target

.text

# --- Short branches stay short (within ±127) ---

# CHECK-LABEL: <short_beql>:
# CHECK: beql
short_beql:
	beql short_target

# CHECK-LABEL: <short_bneq>:
# CHECK: bneq
short_bneq:
	bneq short_target

	.space 60, 0x01

short_target:
	nop

# --- Long branches get expanded ---
# Each Bcc with a target >127 bytes away must become inverted-Bcc + BRW.

	.p2align 4
# CHECK-LABEL: <long_beql>:
# CHECK-NEXT: bneq ${{[0-9]+}}
# CHECK-NEXT: brw
long_beql:
	beql far_target

# CHECK-LABEL: <long_bneq>:
# CHECK-NEXT: beql ${{[0-9]+}}
# CHECK-NEXT: brw
long_bneq:
	bneq far_target

# CHECK-LABEL: <long_bgtr>:
# CHECK-NEXT: bleq ${{[0-9]+}}
# CHECK-NEXT: brw
long_bgtr:
	bgtr far_target

# CHECK-LABEL: <long_bgeq>:
# CHECK-NEXT: blss ${{[0-9]+}}
# CHECK-NEXT: brw
long_bgeq:
	bgeq far_target

# CHECK-LABEL: <long_blss>:
# CHECK-NEXT: bgeq ${{[0-9]+}}
# CHECK-NEXT: brw
long_blss:
	blss far_target

# CHECK-LABEL: <long_bleq>:
# CHECK-NEXT: bgtr ${{[0-9]+}}
# CHECK-NEXT: brw
long_bleq:
	bleq far_target

# CHECK-LABEL: <long_bgtru>:
# CHECK-NEXT: blequ ${{[0-9]+}}
# CHECK-NEXT: brw
long_bgtru:
	bgtru far_target

# CHECK-LABEL: <long_bgequ>:
# CHECK-NEXT: blssu ${{[0-9]+}}
# CHECK-NEXT: brw
long_bgequ:
	bgequ far_target

# CHECK-LABEL: <long_blssu>:
# CHECK-NEXT: bgequ ${{[0-9]+}}
# CHECK-NEXT: brw
long_blssu:
	blssu far_target

# CHECK-LABEL: <long_blequ>:
# CHECK-NEXT: bgtru ${{[0-9]+}}
# CHECK-NEXT: brw
long_blequ:
	blequ far_target

	# Push far_target well out of 8-bit range (>127 bytes from all branches).
	.space 200, 0x01

far_target:
	nop

# --- jXX aliases should also relax ---
# CHECK-LABEL: <jxx_alias>:
# CHECK-NEXT: bneq ${{[0-9]+}}
# CHECK-NEXT: brw
jxx_alias:
	jeql far_target2

	.space 200, 0x01

far_target2:
	nop
