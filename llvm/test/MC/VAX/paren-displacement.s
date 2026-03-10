# RUN: llvm-mc -triple=vax-unknown-netbsdelf -show-encoding %s | FileCheck %s

# Parenthesized constant expression as displacement: (expr)(%reg)
# This syntax appears in NetBSD kernel assembly where C preprocessor macros
# expand to parenthesized expressions, e.g. (CI_NINTR+4)(%r2).

# CHECK: movl	12(%r2), %r0
	movl (4+8)(%r2), %r0

# CHECK: movl	70(%r3), %r1
	movl (100-20-10)(%r3), %r1

# CHECK: addl2	8(%ap), %r0
	addl2 (4+4)(%ap), %r0

# CHECK: movl	%r0, 24(%fp)
	movl %r0, (8*3)(%fp)

# Simple parenthesized constant (no arithmetic)
# CHECK: movl	4(%r2), %r0
	movl (4)(%r2), %r0

# Parenthesized expression without register suffix is still immediate
# CHECK: movl	$12, %r0
	movl (4+8), %r0
