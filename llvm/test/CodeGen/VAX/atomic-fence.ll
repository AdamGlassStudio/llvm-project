; RUN: llc -mtriple=vax-unknown-netbsdelf < %s | FileCheck %s
;
; Atomic fence. VAX has strict memory ordering (no store buffer,
; no OOO execution), so fence expands to a __sync_synchronize libcall.
; MP synchronization uses interlocked instructions (BBSSI/BBCCI/ADAWI).

define void @fence_test() {
; CHECK-LABEL: fence_test:
; CHECK:       calls $0, __sync_synchronize
; CHECK:       ret
  fence seq_cst
  ret void
}
