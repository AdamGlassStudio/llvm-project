// RUN: llvm-mc -triple=vax-unknown-netbsdelf -filetype=obj -position-independent %s -o %t.pic.o
// RUN: llvm-readelf -r %t.pic.o | FileCheck %s --check-prefix=PIC
// RUN: llvm-mc -triple=vax-unknown-netbsdelf -filetype=obj %s -o %t.nopic.o
// RUN: llvm-readelf -r %t.nopic.o | FileCheck %s --check-prefix=NOPIC

// In PIC mode, bare PC-relative references to non-local symbols should use
// R_VAX_PLT32, matching GAS -k behavior. This avoids text relocations in
// shared libraries. The BFD linker resolves PLT32 to direct references for
// symbols defined in the same shared object.

	.text
	.globl	test_calls
test_calls:
	calls	$1, external_func
	rsb

	.globl	test_data_ref
test_data_ref:
	movl	some_data, %r0
	rsb

// PIC: R_VAX_PLT32 {{.*}} external_func
// PIC: R_VAX_PLT32 {{.*}} some_data

// NOPIC: R_VAX_PC32 {{.*}} external_func
// NOPIC: R_VAX_PC32 {{.*}} some_data
