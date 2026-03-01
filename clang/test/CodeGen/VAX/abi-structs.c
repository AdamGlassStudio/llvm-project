// RUN: %clang_cc1 -triple vax-unknown-netbsdelf -emit-llvm -o - %s | FileCheck %s
//
// Verify struct passing and layout on VAX.

struct small { int x; };
struct medium { int a; int b; int c; };

// Small struct — passed byval on VAX (no coercion to scalar).
// CHECK-LABEL: define {{.*}} i32 @read_small(ptr {{.*}} byval(%struct.small) {{.*}} %s)
int read_small(struct small s) {
  return s.x;
}

// Medium struct — also passed byval.
// CHECK-LABEL: define {{.*}} i32 @read_medium(ptr {{.*}} byval(%struct.medium) {{.*}} %m)
int read_medium(struct medium m) {
  return m.a + m.b + m.c;
}

// Struct layout: verify field offsets via GEP.
// CHECK-LABEL: define {{.*}} i32 @access_fields(ptr {{.*}} %s)
int access_fields(struct medium *s) {
  // CHECK: getelementptr inbounds nuw %struct.medium, ptr %{{.*}}, i32 0, i32 0
  // CHECK: getelementptr inbounds nuw %struct.medium, ptr %{{.*}}, i32 0, i32 2
  return s->a + s->c;
}
