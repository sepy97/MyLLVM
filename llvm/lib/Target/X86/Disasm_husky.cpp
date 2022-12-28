//
// Created by Semen Pyankov on 12/17/22.
//

#include "Disasm_husky.h"

bool Disasm_husky::runOnMachineFunction(MachineFunction &MF) {

  for (auto &MBB : MF) {
    std::string BBName = MBB.getFullName();
    if (BBName.find(BasicBlockSubstr) == std::string::npos) {
      continue;
    }
    errs() << "Here is the name of a basic block: " << BBName << "\n";
    for (auto &MI : MBB) {
      // print only write instructions
      if (MI.mayStore()) {
        errs() << "This is a memory write instruction: ";
        MI.print(errs());
      }
    }

    outs() << "DISASM HUSKY!!!\n";
    outs() << "**************************\n";
    outs() << "Contents of MachineBasicBlock:\n";
    outs() << MBB << "\n";
    const BasicBlock *BB = MBB.getBasicBlock();
    outs() << "Contents of BasicBlock corresponding to MachineBasicBlock:\n";
    outs() << BB << "\n";
    outs() << "##########################\n";
  }

  return false;
}
