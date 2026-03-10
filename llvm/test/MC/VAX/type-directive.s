# RUN: llvm-mc -triple=vax-unknown-netbsdelf %s | FileCheck %s

# Test .type directive with all supported syntax forms.
# GAS accepts .type with or without a comma before the type attribute.

# With comma (standard form)
# CHECK: .type	sym_a,@function
.type sym_a, @function

# Without comma, with space (GAS-compatible)
# CHECK: .type	sym_b,@function
.type sym_b @function

# Without comma or space — sym@type merged token (GAS-compatible)
# CHECK: .type	sym_c,@function
.type sym_c@function

# Object type
# CHECK: .type	sym_d,@object
.type sym_d@object

# Notype
# CHECK: .type	sym_e,@notype
.type sym_e, @notype

# Upper case STT_ form
# CHECK: .type	sym_f,@function
.type sym_f, STT_FUNC
