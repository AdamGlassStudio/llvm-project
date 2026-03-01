// RUN: %clang_cc1 -triple vax-unknown-netbsdelf -emit-llvm -o - %s | FileCheck %s
//
// Verify sizeof and alignof for all C types on VAX.
// VAX is 32-bit little-endian with 32-bit alignment for most types.

// Sizes
int sz_char       = sizeof(char);        // CHECK: @sz_char = {{.*}} i32 1
int sz_short      = sizeof(short);       // CHECK: @sz_short = {{.*}} i32 2
int sz_int        = sizeof(int);         // CHECK: @sz_int = {{.*}} i32 4
int sz_long       = sizeof(long);        // CHECK: @sz_long = {{.*}} i32 4
int sz_longlong   = sizeof(long long);   // CHECK: @sz_longlong = {{.*}} i32 8
int sz_ptr        = sizeof(void *);      // CHECK: @sz_ptr = {{.*}} i32 4
int sz_float      = sizeof(float);       // CHECK: @sz_float = {{.*}} i32 4
int sz_double     = sizeof(double);      // CHECK: @sz_double = {{.*}} i32 8
int sz_longdouble = sizeof(long double); // CHECK: @sz_longdouble = {{.*}} i32 8

// Alignments — VAX has relaxed alignment compared to most modern targets.
// long long, double, long double are all 4-byte aligned (not 8).
int al_char       = _Alignof(char);        // CHECK: @al_char = {{.*}} i32 1
int al_short      = _Alignof(short);       // CHECK: @al_short = {{.*}} i32 2
int al_int        = _Alignof(int);         // CHECK: @al_int = {{.*}} i32 4
int al_long       = _Alignof(long);        // CHECK: @al_long = {{.*}} i32 4
int al_longlong   = _Alignof(long long);   // CHECK: @al_longlong = {{.*}} i32 4
int al_ptr        = _Alignof(void *);      // CHECK: @al_ptr = {{.*}} i32 4
int al_float      = _Alignof(float);       // CHECK: @al_float = {{.*}} i32 4
int al_double     = _Alignof(double);      // CHECK: @al_double = {{.*}} i32 4
int al_longdouble = _Alignof(long double); // CHECK: @al_longdouble = {{.*}} i32 4
