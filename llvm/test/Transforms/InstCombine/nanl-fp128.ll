; NOTE: Assertions have been autogenerated by utils/update_test_checks.py UTC_ARGS: --version 5
; RUN: opt < %s -passes=instcombine -S | FileCheck %s

@empty = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@dec = private unnamed_addr constant [2 x i8] c"1\00", align 1
@hex = private unnamed_addr constant [4 x i8] c"0xf\00", align 1

define fp128 @nanl_empty() {
; CHECK-LABEL: define fp128 @nanl_empty() {
; CHECK-NEXT:    ret fp128 0xL00000000000000007FFF800000000000
;
  %res = call fp128 @nanl(ptr @empty)
  ret fp128 %res
}

define fp128 @nanl_dec() {
; CHECK-LABEL: define fp128 @nanl_dec() {
; CHECK-NEXT:    ret fp128 0xL00000000000000017FFF800000000000
;
  %res = call fp128 @nanl(ptr @dec)
  ret fp128 %res
}

define fp128 @nanl_hex() {
; CHECK-LABEL: define fp128 @nanl_hex() {
; CHECK-NEXT:    ret fp128 0xL000000000000000F7FFF800000000000
;
  %res = call fp128 @nanl(ptr @hex)
  ret fp128 %res
}

declare fp128 @nanl(ptr)
