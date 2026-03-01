// RUN: %clang_cc1 -triple vax-unknown-netbsdelf -emit-llvm -o - %s | FileCheck %s
//
// Verify struct return uses sret (hidden first pointer argument).
// On VAX/NetBSD, the sret pointer is passed as the first argument.

struct pair { int x; int y; };

// CHECK-LABEL: define {{.*}} void @make_pair(ptr {{.*}} sret(%struct.pair) {{.*}} %agg.result, i32 {{.*}} %a, i32 {{.*}} %b)
struct pair make_pair(int a, int b) {
  struct pair p = {a, b};
  return p;
}

struct triple { int a; int b; int c; };

// CHECK-LABEL: define {{.*}} void @make_triple(ptr {{.*}} sret(%struct.triple) {{.*}} %agg.result, i32 {{.*}} %x)
struct triple make_triple(int x) {
  struct triple t = {x, x+1, x+2};
  return t;
}

// Caller side: verify sret call convention.
// CHECK-LABEL: define {{.*}} i32 @use_pair()
// CHECK: %[[TMP:.*]] = alloca %struct.pair
// CHECK: call void @make_pair(ptr {{.*}} %[[TMP]], i32 {{.*}} 10, i32 {{.*}} 20)
int use_pair(void) {
  struct pair p = make_pair(10, 20);
  return p.x + p.y;
}
