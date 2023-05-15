# Building MyLLVM into build directory using lld, ninja

cmake -S llvm -B build -G Ninja -DLLVM_ENABLE_PROJECTS='clang;lld;lldb' -DCMAKE_BUILD_TYPE=Debug -DLLVM_TARGETS_TO_BUILD=X86 -DLLVM_USE_LINKER=lld

cmake --build build
