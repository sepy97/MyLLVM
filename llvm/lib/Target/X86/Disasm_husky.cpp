//
// Created by Semen Pyankov on 12/17/22.
//

#include "Disasm_husky.h"

bool Disasm_husky::runOnMachineFunction(MachineFunction &MF) {

  const auto &SubTarget = MF.getSubtarget(); // MF.getSubtarget<X86Subtarget>();
  auto XII = SubTarget.getInstrInfo();

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

        DebugLoc DL = MI.getDebugLoc();
        //auto SaveRsp = BuildMI(MBB, MI, DL, XII->get(X86::MOV64mr));
        //auto SaveRsp = BuildMI(MBB, MI, DL, XII->get(X86::MOV64mr)).addReg(0).addImm(1).addReg(0).addExternalSymbol("currentRsp").addReg(0).addReg(X86::RSP);
        //SaveRsp.addReg(0).addExternalSymbol("currentRsp");
        //SaveRsp.addReg(0).addReg(X86::RSP);

        errs() << "Save RSP to the reserved register!\n";
        //SaveRsp->print(errs());

        // save RSP to the reserved register
        // change RSP to checkpointSP
        // push memory address
        // push memory item
        // save RSP to checkpointSP (checkpointSP should change bcoz we already checkpointed two more values)
        // change RSP back (get it from the reserved register)
      }
    }

    outs() << "DISASM HUSKY!!!\n";
    /*
    outs() << "**************************\n";
    outs() << "Contents of MachineBasicBlock:\n";
    outs() << MBB << "\n";
    const BasicBlock *BB = MBB.getBasicBlock();
    outs() << "Contents of BasicBlock corresponding to MachineBasicBlock:\n";
    outs() << BB << "\n";
    outs() << "##########################\n";
     */
  }

  return false;
}
