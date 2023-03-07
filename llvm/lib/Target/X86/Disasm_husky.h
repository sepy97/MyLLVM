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

  unsigned getTraceKey(std::string *BBName);

  StringRef getPassName() const override { return "Disasm_HUSKY pass"; }

private:
  // We're looking for basic blocks with a name containing one of these substrings
  std::string TraceSubstr      = "ucitrace";
  std::string AbortSubstr      = "uciabort";
  std::string CommitSubstr     = "ucicommit";

  std::map<int, MachineBasicBlock *> TraceMap;
  std::map<int, MachineBasicBlock *> AbortMap;
  MachineBasicBlock *CommitBasicBlock = nullptr; // a single commit block for all traces

};

char Disasm_husky::ID = 0;

} // namespace

INITIALIZE_PASS(Disasm_husky, "HUSKY", "Disasm_HUSKY pass",
                true, // is CFG only?
                true  // is analysis?
)

namespace llvm {

FunctionPass *createDisasm_husky() { return new Disasm_husky(); }

} // namespace llvm

#endif // LLVM_LIB_TARGET_X86_DISASM_HUSKY_H
