//
// Created by Semen Pyankov on 12/17/22.
//

#include "Disasm_husky.h"

bool Disasm_husky::runOnMachineFunction(MachineFunction &MF) {

  const auto &SubTarget = MF.getSubtarget();
  auto XII = SubTarget.getInstrInfo();

  bool BBFound      = false; // condition shows if we've already found the necessary Basic Block
  bool Checkpointed = false; // condition shows if we entered the speculative region
  bool Rolledback   = false; // condition shows if we exited the speculative region and rolled back memory writes
  bool Committed    = false; // condition shows if we exited the speculative region normally
  unsigned MemoryWriteCtr = 0; // counter of checkpointed memory writes
  Register ChkptReg;

  for (auto &MBB : MF) {
    std::string BBName = MBB.getFullName();
    // TODO: remove the usage of redundant TRACEDLOOPBODY substr

    if (!BBFound){
      if (BBName.find(BasicBlockSubstr) == std::string::npos) {
        continue;
      }

    }
    // Starting from here we're working with a TRACEDLOOPBODY Basic Block
    BBFound = true;

    if (BBFound && BBName.find(CheckpointSubstr) != std::string::npos) {
      Checkpointed = true;
      // Here we checkpoint before every memory write

      errs() << "Here is the name of a basic block: " << BBName << "\n";
      auto MRI = &MF.getRegInfo();
      auto FirstInstruction = MBB.instr_begin();
      DebugLoc FirstDL = MBB.findDebugLoc(FirstInstruction);
      ChkptReg = MRI->createVirtualRegister(&X86::GR64RegClass);

      // movq    $10000, %ChkptReg
      auto ChkptInit = BuildMI(MBB, FirstInstruction, FirstDL, XII->get(X86::MOV64ri32), ChkptReg);
      ChkptInit.addImm(-10000);
      ChkptInit->print(errs());

      // subq    %rsp, %ChkptReg
      auto AddChkpt = BuildMI(MBB, FirstInstruction, FirstDL, XII->get(X86::ADD64rr), ChkptReg);
      AddChkpt.addReg(ChkptReg);
      AddChkpt.addReg(X86::RSP);
      AddChkpt->print(errs());

      for (auto &MI : MBB) {
        // print only write instructions
        if (MI.mayStore()) {
          errs() << "This is a memory write instruction: ";
          MI.print(errs());
          MemoryWriteCtr++;

          DebugLoc DL = MI.getDebugLoc();

          const MCInstrDesc &Desc = MI.getDesc();
          int MemRefBegin = X86II::getMemoryOperandNo(Desc.TSFlags);
          MemRefBegin += X86II::getOperandBias(Desc);
          errs() << "MemRefBegin: " << MemRefBegin << "\n";

          // Create virtual register and save RSP to the reserved register
          auto SaveRsp = BuildMI(MBB, MI, DL, XII->get(X86::MOV64rr));
          auto VReg = MRI->createVirtualRegister(&X86::GR64RegClass);
          SaveRsp.addDef(VReg);
          SaveRsp.addReg(X86::RSP);
          SaveRsp->print(errs());

          // change RSP to checkpointSP
          auto ShiftRsp = BuildMI(MBB, MI, DL, XII->get(X86::MOV64rr));
          ShiftRsp.addDef(X86::RSP);
          ShiftRsp.addReg(ChkptReg);
          errs() << "Change RSP to checkpointSP!\n";
          ShiftRsp->print(errs());

          // TODO: next two instructions should be in one instruction (PUSH64m with memory address as an operand)
          // move memory address to the virtual register
          auto DestReg = MRI->createVirtualRegister(&X86::GR64RegClass);
          auto MoveDestToReg = BuildMI(MBB, MI, DL, XII->get(X86::LEA64r));
          MoveDestToReg.addDef(DestReg);
          MoveDestToReg.add(MI.getOperand(MemRefBegin + X86::AddrBaseReg));
          MoveDestToReg.add(MI.getOperand(MemRefBegin + X86::AddrScaleAmt));
          MoveDestToReg.add(MI.getOperand(MemRefBegin + X86::AddrIndexReg));
          MoveDestToReg.add(MI.getOperand(MemRefBegin + X86::AddrDisp));
          MoveDestToReg.add(MI.getOperand(MemRefBegin + X86::AddrSegmentReg));
          errs() << "Move memory address to the virtual register!\n";
          MoveDestToReg->print(errs());

          // push memory address
          auto PushMemAddr = BuildMI(MBB, MI, DL, XII->get(X86::PUSH64r));
          PushMemAddr.addReg(DestReg);
          PushMemAddr->print(errs());

          // push memory item
          auto PushMemItem = BuildMI(MBB, MI, DL, XII->get(X86::PUSH64rmm)); // USE MachineIRBuilder @@@
          PushMemItem.addReg(DestReg);
          PushMemItem.addImm(1).addReg(0);
          PushMemItem.addImm(0).addReg(0);
          PushMemItem->print(errs());

          // save RSP to checkpointSP (checkpointSP should change bcoz we already checkpointed two more values)
          auto ShiftChkpt = BuildMI(MBB, MI, DL, XII->get(X86::MOV64rr), ChkptReg);
          ShiftChkpt.addReg(X86::RSP);
          errs() << "Save RSP to checkpointSP!\n";
          ShiftChkpt->print(errs());

          // change RSP back (get it from the reserved register)
          auto RestoreRsp = BuildMI(MBB, MI, DL, XII->get(X86::MOV64rr));
          RestoreRsp.addDef(X86::RSP);
          RestoreRsp.addReg(VReg);
          RestoreRsp->print(errs());
        }
      }

      errs() << "CHECKPOINTED!\n";
    }

    if (Checkpointed && BBName.find(CommitSubstr) != std::string::npos) {
      Committed    = true;
      Checkpointed = false; // this should be the end of the speculative region; we can start checkpointing again
      // Here no action is needed, just discard the checkpointed values

      errs() << "COMMITTED!\n";
    }
    else if (Checkpointed && BBName.find(RollbackSubstr) != std::string::npos) {
      Rolledback   = true;
      Checkpointed = false; // this should be the end of the speculative region; we can start checkpointing again
      // Here we rollback memory writes that were checkpointed before

      auto MRI = &MF.getRegInfo();
      auto FirstInstruction = MBB.instr_begin();
      DebugLoc FirstDL = MBB.findDebugLoc(FirstInstruction);

      // Save rsp
      auto SaveRsp = BuildMI(MBB, FirstInstruction, FirstDL, XII->get(X86::MOV64rr));
      auto VReg = MRI->createVirtualRegister(&X86::GR64RegClass);
      SaveRsp.addDef(VReg);
      SaveRsp.addReg(X86::RSP);
      errs() << "Save rsp!\n";
      SaveRsp->print(errs());

      // Change rsp to checkpointSP
      auto ShiftRsp = BuildMI(MBB, FirstInstruction, FirstDL, XII->get(X86::MOV64rr));
      ShiftRsp.addDef(X86::RSP);
      ShiftRsp.addReg(ChkptReg);
      errs() << "Change rsp to checkpointSP!\n";
      ShiftRsp->print(errs());

      while (MemoryWriteCtr > 0) {
        MemoryWriteCtr--;



        // pop memory item
        auto MemItem = MRI->createVirtualRegister(&X86::GR64RegClass);
        auto PopMemItem = BuildMI(MBB, FirstInstruction, FirstDL, XII->get(X86::POP64r));
        PopMemItem.addDef(MemItem);
        errs() << "Pop memory item!\n";
        PopMemItem->print(errs());

        // pop memory address
        auto MemAddr = MRI->createVirtualRegister(&X86::GR64RegClass);
        auto PopMemAddr = BuildMI(MBB, FirstInstruction, FirstDL, XII->get(X86::POP64r));
        PopMemAddr.addDef(MemAddr);
        errs() << "Pop memory address!\n";
        PopMemAddr->print(errs());

        // move memory item to the memory address
        auto MoveMemItemToAddr = BuildMI(MBB, FirstInstruction, FirstDL, XII->get(X86::MOV64mr));
        MoveMemItemToAddr.addReg(MemAddr);
        MoveMemItemToAddr.addImm(1).addReg(0);
        MoveMemItemToAddr.addImm(0).addReg(0);
        MoveMemItemToAddr.addReg(MemItem);
        errs() << "Move memory item to the memory address!\n";
        MoveMemItemToAddr->print(errs());

/*
        // move memory address to the virtual register
        auto DestReg = MRI->createVirtualRegister(&X86::GR64RegClass);
        auto MoveDestToReg = BuildMI(MBB, FirstInstruction, FirstDL, XII->get(X86::LEA64r));
        MoveDestToReg.addDef(DestReg);
        MoveDestToReg.addReg(X86::RSP);
        MoveDestToReg.addImm(1).addReg(0);
        MoveDestToReg.addImm(0).addReg(0);
        MoveDestToReg->print(errs());

        // move memory item to the virtual register
        auto SrcReg = MRI->createVirtualRegister(&X86::GR64RegClass);
        auto MoveSrcToReg = BuildMI(MBB, FirstInstruction, FirstDL, XII->get(X86::MOV64rm));
        MoveSrcToReg.addDef(SrcReg);
        MoveSrcToReg.addReg(DestReg);
        MoveSrcToReg.addImm(1).addReg(0);
        MoveSrcToReg.addImm(0).addReg(0);
        MoveSrcToReg->print(errs());

        // move memory item to the memory address
        auto MoveMemItem = BuildMI(MBB, FirstInstruction, FirstDL, XII->get(X86::MOV64mr));
        MoveMemItem.addReg(DestReg);
        MoveMemItem.addImm(1).addReg(0);
        MoveMemItem.addImm(0).addReg(0);
*/

      }

      // change RSP back (get it from the reserved register)
      auto RestoreRsp = BuildMI(MBB, FirstInstruction, FirstDL, XII->get(X86::MOV64rr));
      RestoreRsp.addDef(X86::RSP);
      RestoreRsp.addReg(VReg);
      errs() << "Change RSP back!\n";
      RestoreRsp->print(errs());

      errs() << "ROLLEDBACK!\n";
    }




  }

  return false;
}
