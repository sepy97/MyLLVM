// Test that verifies TSan runtime doesn't contain compiler-emitted
// memcpy/memmove calls. It builds the binary with TSan and check's
// its objdump.

// This could fail if using a static libunwind because that static libunwind
// could be uninstrumented and contain memcpy/memmove calls not intercepted by
// tsan.
// REQUIRES: shared_unwind

// XFAIL: !compiler-rt-optimized

// RUN: %clang_tsan -O1 %s -o %t
// RUN: llvm-objdump -d -l %t | FileCheck %s

int main() {
  return 0;
}

// CHECK-NOT: callq {{.*<(__interceptor_)?mem(cpy|set)>}}
// tail calls:
// CHECK-NOT: jmpq {{.*<(__interceptor_)?mem(cpy|set)>}}

