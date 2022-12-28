//
// Created by Semen Pyankov on 12/17/22.
//

#ifndef LLVM_LIB_TARGET_X86_DISASM_HUSKY_H
#define LLVM_LIB_TARGET_X86_DISASM_HUSKY_H

#include "X86.h"
#include "X86InstrInfo.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"

using namespace llvm;

namespace {

class Disasm_husky : public MachineFunctionPass {
public:
  static char ID;

  Disasm_husky() : MachineFunctionPass(ID) {
    initializeDisasm_huskyPass(*PassRegistry::getPassRegistry());
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

  StringRef getPassName() const override { return "Disasm_husky pass"; }

private:
  // We're looking for a basic block with a name containing this substring
  std::string BasicBlockSubstr = "TRACEDLOOPBODY";
};

char Disasm_husky::ID = 0;

} // namespace

INITIALIZE_PASS(Disasm_husky, "husky", "Disasm_husky pass",
                true, // is CFG only?
                true  // is analysis?
)

namespace llvm {

FunctionPass *createDisasm_husky() { return new Disasm_husky(); }

} // namespace llvm

#endif // LLVM_LIB_TARGET_X86_DISASM_HUSKY_H
